// The fetch pipeline and the scoring pass. Split from the construction half
// in tag_cluster_page.cpp, which is all widgets.
#include <app/tag_cluster_page.h>
#include <app/icons.h>
#include <app/tag_preview_fetcher.h>
#include <app/tag_preview_popup.h>
#include <core/cluster_filter.h>
#include <core/danbooru_index.h>
#include <core/entry.h>
#include <core/tag_facets.h>
#include <QCheckBox>
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <cmath>

using namespace Qt::StringLiterals;

namespace tc {

namespace cluster {

constexpr double kDanbooruTotalPosts = 9000000.0;
constexpr qint64 kGlobalCountFloor = 20;
constexpr int kPostsPerPage = 200;

// Danbooru asks for a gap between requests. Anything faster gets throttled
// anyway, so waiting is not a cost.
constexpr int kInterPageDelayMs = 500;

QString promptForm(const QString& tag);

} // namespace cluster

using namespace cluster;

// ---- Fetch

void TagClusterPage::startFetch()
{
    if (m_fetching) return;

    const QString raw = m_tagInput->text().trimmed();
    if (raw.isEmpty()) return;

    // A tag pasted out of a prompt carries the composer's backslash escapes
    // and spaces; Danbooru wants neither.
    QString target = raw;
    target.remove(u'\\');
    target.replace(u' ', u'_');

    m_targetTag = target;
    m_fetchedSolo = m_solo->isChecked();
    m_fetchedSingleChar = m_singleChar->isChecked();

    clearResultRows();
    m_copyEdit->clear();
    m_copyBtn->setEnabled(false);
    m_createEntryBtn->setEnabled(false);
    m_copyright.clear();
    m_tagCounts.clear();
    m_copyrightCounts.clear();
    m_usedPosts = 0;
    m_fetchedPosts = 0;
    m_dataReady = false;
    m_pageTarget = m_pages->value();
    m_currentPage = 0;
    m_pageReply = nullptr;

    const int generation = ++m_generation;

    m_fetching = true;
    setFetchRunning(true);
    setStatus(u"Fetching posts..."_s);
    m_emptyLabel->setText(u"Fetching..."_s);

    // Independent of the page pipeline, so it is not worth blocking on.
    clearPreview();
    m_previewForTag = m_targetTag;
    m_preview->fetch(m_targetTag);

    fetchNextPage(generation);
}

void TagClusterPage::cancelFetch()
{
    if (!m_fetching) return;

    // Any reply in flight, and any pending inter-page timer, captured the old
    // generation and will drop out on their own.
    ++m_generation;
    if (m_pageReply) {
        m_pageReply->abort();
        m_pageReply = nullptr;
    }

    // Score whatever was collected: a partial sample is still a sample, and
    // the thresholds can be tuned against it without re-fetching.
    finishFetch();

    if (m_usedPosts > 0) {
        setStatus(u"Cancelled. Kept %1 posts, %2 tags - tweak any threshold to refine."_s
                      .arg(m_usedPosts)
                      .arg(m_tagCounts.size()));
        return;
    }

    setStatus(u"Fetch cancelled."_s);
    m_emptyLabel->setText(u"Fetch cancelled."_s);
    setResultsEmpty(true);
}

void TagClusterPage::fetchNextPage(int generation)
{
    if (generation != m_generation) return; // cancelled during the delay

    QUrlQuery query;
    query.addQueryItem(u"limit"_s, QString::number(kPostsPerPage));
    query.addQueryItem(u"page"_s, QString::number(m_currentPage + 1));

    // Danbooru reads a space as AND, so +solo goes in the tag string itself.
    query.addQueryItem(u"tags"_s, m_fetchedSolo ? m_targetTag + u" solo"_s : m_targetTag);

    QUrl url(u"https://danbooru.donmai.us/posts.json"_s);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, u"TagComposer/1.0"_s);

    QNetworkReply* reply = m_network->get(request);
    m_pageReply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
        reply->deleteLater();
        if (m_pageReply == reply) m_pageReply = nullptr;
        if (generation != m_generation) return; // cancel already finalised this run

        if (reply->error() != QNetworkReply::NoError) {
            setStatus(u"Network error: %1"_s.arg(reply->errorString()));
            m_fetching = false;
            setFetchRunning(false);
            m_emptyLabel->setText(u"Network error - try again."_s);
            setResultsEmpty(true);
            return;
        }

        const QJsonArray posts = QJsonDocument::fromJson(reply->readAll()).array();
        if (!posts.isEmpty()) {
            processPage(posts);
            ++m_currentPage;
            setStatus(u"Fetching... %1 posts, %2 tags so far."_s.arg(m_usedPosts)
                          .arg(m_tagCounts.size()));
        }

        setProgress(std::min(m_currentPage, m_pageTarget), m_pageTarget);

        // An empty page means the character has fewer posts than asked for,
        // which is a normal finish rather than an error.
        if (posts.isEmpty() || m_currentPage >= m_pageTarget) {
            finishFetch();
            return;
        }
        QTimer::singleShot(kInterPageDelayMs, this,
                           [this, generation]() { fetchNextPage(generation); });
    });
}

void TagClusterPage::processPage(const QJsonArray& posts)
{
    for (const QJsonValue& value : posts) {
        const QJsonObject post = value.toObject();
        ++m_fetchedPosts;

        // An alt-form post lists the base character and every variant, and
        // their outfits pull the cluster off-model.
        if (m_fetchedSingleChar) {
            const QStringList characters =
                post[u"tag_string_character"_s].toString().split(u' ', Qt::SkipEmptyParts);
            if (characters.size() != 1) continue;
        }

        // One increment per post, not per occurrence: this is a document
        // frequency, and a post lists each general tag once.
        for (const QString& tag :
             post[u"tag_string_general"_s].toString().split(u' ', Qt::SkipEmptyParts))
            ++m_tagCounts[tag];
        ++m_usedPosts;

        for (const QString& copyright :
             post[u"tag_string_copyright"_s].toString().split(u' ', Qt::SkipEmptyParts))
            ++m_copyrightCounts[copyright];
    }
}

void TagClusterPage::finishFetch()
{
    m_fetching = false;
    setFetchRunning(false);
    m_dataReady = true;

    // Once per fetch, not per recompute: the sample does not change under the
    // sliders.
    m_copyright = u"No Copyright"_s;
    int best = 0;
    for (auto it = m_copyrightCounts.cbegin(); it != m_copyrightCounts.cend(); ++it) {
        if (it.value() <= best) continue;
        best = it.value();
        m_copyright = it.key();
    }

    recompute();
}

// ---- Scoring

void TagClusterPage::recompute()
{
    if (!m_dataReady) return;

    auto bail = [this](const QString& status, const QString& empty) {
        clearResultRows();
        setStatus(status);
        m_copyEdit->clear();
        m_copyBtn->setEnabled(false);
        m_createEntryBtn->setEnabled(false);
        m_emptyLabel->setText(empty);
        setResultsEmpty(true);
    };

    if (!m_danbooru) {
        bail(u"danbooru.csv index not loaded - cannot compute PMI."_s,
             u"danbooru.csv not loaded."_s);
        return;
    }
    if (m_usedPosts <= 0) {
        bail(u"No usable posts (%1 fetched). Check the tag name, or turn off +solo /\n"
             u"'single character tag only'."_s.arg(m_fetchedPosts),
             u"No usable posts - check the tag name and re-fetch."_s);
        return;
    }

    const ClusterFilter filter = filterFromEditor();
    const double minPmi = m_minPmi->value() / 100.0;

    // The slider is a share of this fetch's posts, resolved here to a count.
    // Zero means no floor: every tag in the map has a count of at least one.
    const int minCount = qRound(m_minPct->value() / 1000.0 * m_usedPosts);

    struct Scored {
        QString tag;
        double pmi;

        // PMI scaled by sqrt(frequency), so a tag that is both common and
        // distinctive outranks one that is distinctive only because it is
        // rare. Used to break ties, not to order the list.
        double weight;
    };
    QList<Scored> scored;

    for (auto it = m_tagCounts.cbegin(); it != m_tagCounts.cend(); ++it) {
        const QString& tag = it.key();
        const int count = it.value();
        if (count < minCount) continue;

        // Both indexes key on the space form; the wire form has underscores.
        // A tag with no facets still reaches keep(), which is what makes
        // blacklist keep it and whitelist drop it.
        const QString lookup = normalizeTag(tag);
        if (!filter.keep(m_facets->facetsFor(lookup))) continue;

        const qint64 globalCount = m_danbooru->tagCount(lookup);
        if (globalCount < kGlobalCountFloor) continue;

        // Document-level PMI: how much likelier this tag is in the character's
        // posts than in the corpus at large.
        const double pInCluster = double(count) / double(m_usedPosts);
        const double pInCorpus = double(globalCount) / kDanbooruTotalPosts;
        const double pmi = std::log(pInCluster / pInCorpus);
        if (pmi < minPmi) continue;

        scored.append({tag, pmi, pmi * std::sqrt(pInCluster)});
    }

    // Ordered by raw PMI so the list matches the score column the slider acts
    // on; the weight only settles ties.
    std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
        if (a.pmi != b.pmi) return a.pmi > b.pmi;
        return a.weight > b.weight;
    });

    clearResultRows();
    for (qsizetype i = 0; i < scored.size(); ++i) {
        m_rows.append({nullptr, scored[i].tag, true});
        QWidget* row = makeResultRow(scored[i].tag, scored[i].pmi, i);
        m_rows[i].widget = row;
        m_resultsLayout->insertWidget(int(i), row);
    }

    QString posts = u"%1 posts"_s.arg(m_usedPosts);
    if (const int skipped = m_fetchedPosts - m_usedPosts; skipped > 0)
        posts += u" (%1 multi-character skipped)"_s.arg(skipped);

    setStatus(u"%1, %2 tags. Showing %3 (tweak any threshold to refine)."_s.arg(posts)
                  .arg(m_tagCounts.size())
                  .arg(scored.size()));

    m_copyBtn->setEnabled(!scored.isEmpty());
    m_createEntryBtn->setEnabled(!scored.isEmpty());
    if (scored.isEmpty()) m_emptyLabel->setText(u"No tags matched the current filters."_s);
    setResultsEmpty(scored.isEmpty());

    rebuildCopyString();
}

void TagClusterPage::scheduleRecompute()
{
    if (!m_dataReady) return;
    m_recomputeTimer->start(); // restarts if already pending
}

void TagClusterPage::markStale()
{
    if (!m_dataReady) return;
    m_dataReady = false;

    setStatus(u"Parameters changed - click Fetch to refresh."_s);
    clearResultRows();
    m_copyEdit->clear();
    m_copyBtn->setEnabled(false);
    m_createEntryBtn->setEnabled(false);
    m_emptyLabel->setText(u"Re-fetch to refresh."_s);
    setResultsEmpty(true);
}

// ---- Result rows

void TagClusterPage::clearResultRows()
{
    // Deleted outright rather than deleteLater: a fast recompute would
    // otherwise stack the old rows behind the new ones until the event loop
    // caught up, and the layout would reflow around both.
    while (m_resultsLayout->count() > 1) {
        QLayoutItem* item = m_resultsLayout->takeAt(0);
        delete item->widget();
        delete item;
    }
    m_rows.clear();
}

QWidget* TagClusterPage::makeResultRow(const QString& tag, double pmi, qsizetype index)
{
    auto* row = new QWidget(m_resultsContainer);
    row->setObjectName(u"DatasetResultRow"_s);

    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(6, 2, 6, 2);
    layout->setSpacing(6);

    auto* remove = new QPushButton(row);
    remove->setObjectName(u"TagRemoveBtn"_s);
    remove->setFixedSize(18, 18);
    remove->setCursor(Qt::PointingHandCursor);
    icons::applyStates(remove, icons::close, 9, QColor(0x3a, 0x3a, 0x3a),
                       QColor(0xcc, 0x33, 0x33));

    // Held in the wire form for the API and the lookups, shown in the space
    // form like everywhere else in the app.
    const QString display = normalizeTag(tag);

    auto* label = new QLabel(display, row);
    label->setObjectName(u"DatasetResultTag"_s);

    // The same badge the composer and the entry panel use for a tag with no
    // facets defined.
    auto* badge = new QLabel(u"?"_s, row);
    badge->setObjectName(u"TagNoFacetBadge"_s);
    badge->setAttribute(Qt::WA_StyledBackground, true);
    badge->setVisible(!m_facets->isDefined(display));

    // The raw PMI, not the tie-break weight: the slider is in these units, so
    // the column has to be what it compares against.
    auto* score = new QLabel(QString::number(pmi, 'f', 2), row);
    score->setObjectName(u"DatasetResultScore"_s);
    score->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    score->setFixedWidth(48);
    score->setToolTip(u"PMI: how much likelier this tag is here than corpus-wide."_s);

    layout->addWidget(remove);
    layout->addWidget(label, 1);
    layout->addWidget(badge);
    layout->addWidget(score);

    connect(remove, &QPushButton::clicked, this, [this, index]() {
        if (index < 0 || index >= m_rows.size()) return;
        m_rows[index].included = false;
        m_rows[index].widget->setVisible(false);
        rebuildCopyString();
    });

    installRowMenu(row, tag);
    installRowMenu(label, tag);
    return row;
}

void TagClusterPage::installRowMenu(QWidget* widget, const QString& tag)
{
    const QString wikiTag = normalizeTag(tag);

    widget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(widget, &QWidget::customContextMenuRequested, this, [this, wikiTag](const QPoint&) {
        QMenu menu;
        QAction* wiki = menu.addAction(u"Wiki"_s);
        installWikiPeek(menu, wiki, wikiTag, previewPopup());
        QAction* facets = menu.addAction(u"Edit facets"_s);

        const QList<QPair<QString, QString>> quick{
            {u"character"_s, m_quickCharacter},
            {u"copyright"_s, m_quickCopyright},
            {u"trigger word"_s, m_quickTrigger},
            {u"style"_s, m_quickStyle},
        };

        QHash<QAction*, QString> quickActions;
        for (const auto& [name, facet] : quick) {
            if (facet.isEmpty()) continue;
            if (quickActions.isEmpty()) menu.addSeparator();
            quickActions.insert(menu.addAction(u"Quick add as %1 (%2)"_s.arg(name, facet)), facet);
        }

        QAction* chosen = menu.exec(QCursor::pos());
        previewPopup()->dismiss();

        if (chosen == wiki)
            emit wikiRequested(wikiTag);
        else if (chosen == facets)
            emit facetEditorRequested(wikiTag);
        else if (chosen && quickActions.contains(chosen))
            emit quickFacetRequested(wikiTag, quickActions.value(chosen));
    });
}

void TagClusterPage::rebuildCopyString()
{
    QStringList parts{promptForm(m_targetTag)};
    if (m_copyright != u"No Copyright"_s) parts << promptForm(m_copyright);
    for (const ResultRow& row : m_rows)
        if (row.included) parts << promptForm(row.tag);

    m_copyEdit->setPlainText(parts.join(u", "_s));
}

// ---- Filters

ClusterFilter TagClusterPage::filterFromEditor() const
{
    ClusterFilter filter;
    filter.mode =
        m_whitelist->isChecked() ? ClusterFilter::Mode::Whitelist : ClusterFilter::Mode::Blacklist;

    for (const QString& rawLine : m_filterEdit->toPlainText().split(u'\n')) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(u'#')) continue;

        QStringList positive;
        for (const QString& token : line.split(u',', Qt::SkipEmptyParts)) {
            const QString trimmed = token.trimmed();
            if (trimmed.isEmpty()) continue;

            if (!trimmed.startsWith(u'-')) {
                positive << trimmed;
                continue;
            }
            const QString name = trimmed.sliced(1).trimmed();
            if (!name.isEmpty() && !filter.negations.contains(name)) filter.negations << name;
        }
        if (!positive.isEmpty()) filter.rules << positive;
    }
    return filter;
}

void TagClusterPage::loadFilters()
{
    if (m_filtersPath.isEmpty() || !QFile::exists(m_filtersPath)) return;

    const ClusterFilter filter = ClusterFilter::load(m_filtersPath);
    (filter.mode == ClusterFilter::Mode::Whitelist ? m_whitelist : m_blacklist)->setChecked(true);

    QStringList lines;
    if (!filter.negations.isEmpty()) {
        QStringList prefixed;
        prefixed.reserve(filter.negations.size());
        for (const QString& negation : filter.negations) prefixed << u"-"_s + negation;
        lines << prefixed.join(u", "_s);
    }
    for (const QStringList& rule : filter.rules) lines << rule.join(u", "_s);

    m_filterEdit->setPlainText(lines.join(u"\n"_s));
    m_filterStatus->setText(u"Loaded %1 rule(s), %2 negation(s)."_s.arg(filter.rules.size())
                                .arg(filter.negations.size()));
}

void TagClusterPage::saveFilters()
{
    if (m_filtersPath.isEmpty()) return;

    QDir().mkpath(QFileInfo(m_filtersPath).absolutePath());

    const ClusterFilter filter = filterFromEditor();
    if (!filter.save(m_filtersPath)) {
        m_filterStatus->setText(u"Could not write cluster_filters.fct."_s);
        return;
    }
    m_filterStatus->setText(u"Saved %1 rule(s), %2 negation(s)."_s.arg(filter.rules.size())
                                .arg(filter.negations.size()));
}

} // namespace tc
