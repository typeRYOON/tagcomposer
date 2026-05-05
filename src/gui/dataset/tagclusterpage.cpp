#include <gui/dataset/tagclusterpage.h>
#include <gui/widgets/appscrollbar.h>
#include <core/facetindex.h>
#include <utils/appconfig.h>
#include <utils/stringutils.h>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QFrame>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrlQuery>
#include <QUrl>
#include <QFile>
#include <QDir>
#include <QTimer>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QMouseEvent>
#include <QPixmap>
#include <cmath>

using namespace utils;

namespace gui {

static const QString DANBOORU_BASE = "https://danbooru.donmai.us/posts.json";

// In-app tags use spaces; the Danbooru API expects underscores. Used both for
// the cluster query and for the wiki/post preview chain.
static QString tagToApiSlug(const QString& tag)
{
    QString s = tag.toLower();
    s.replace(' ', '_');
    return s;
}

// Composer-friendly tag formatting for the copy string.
static QString fmtTag(const QString& tag)
{
    return QString(tag).replace('_', ' ').replace('(', "\\(").replace(')', "\\)");
}

// ---- Layout dimensions kept here so tweaks live in one place
constexpr int kPanelWidth = 280;
constexpr int kPreviewPanelWidth = 320;
constexpr int kPreviewMaxW = 296; // panel width minus 12*2 margins
constexpr int kPreviewMaxH = 420;

// PMI slider stores integer hundredths so it can drive the live recompute
// without the awkward QDoubleSpinBox keyboard increments.
constexpr int kPmiSliderMin = 0;      // 0.00
constexpr int kPmiSliderMax = 500;    // 5.00
constexpr int kPmiSliderDefault = 50; // 0.50

TagClusterPage::TagClusterPage(core::FacetIndex* facets, QWidget* parent)
    : QWidget(parent), m_facets(facets)
{
    setObjectName("TagClusterPage");

    m_nam = new QNetworkAccessManager(this);

    // Coalesces recompute bursts; without it, a slider drag would
    // rebuild result rows 60+ times per second and visibly flicker.
    m_recomputeTimer = new QTimer(this);
    m_recomputeTimer->setSingleShot(true);
    m_recomputeTimer->setInterval(150);
    connect(m_recomputeTimer, &QTimer::timeout, this, &TagClusterPage::recompute);

    // 50 px panel header that mirrors WorkflowEditPage's #WfEditHeader.
    auto makeSectionHeader = [this](const QString& title) -> QWidget* {
        auto* header = new QWidget(this);
        header->setObjectName("DatasetSectionHeader");
        header->setAttribute(Qt::WA_StyledBackground, true);
        header->setFixedHeight(50);

        auto* l = new QHBoxLayout(header);
        l->setContentsMargins(16, 12, 16, 12);
        l->setSpacing(8);

        auto* lbl = new QLabel(title, header);
        lbl->setObjectName("DatasetSectionTitle");
        l->addWidget(lbl);
        l->addStretch();

        return header;
    };

    // ---- Params panel (left)
    auto* paramsPanel = new QWidget(this);
    paramsPanel->setObjectName("DatasetParamsPanel");
    paramsPanel->setAttribute(Qt::WA_StyledBackground, true);
    paramsPanel->setFixedWidth(kPanelWidth);

    auto* pl = new QVBoxLayout(paramsPanel);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->setSpacing(0);

    // Tag-section body (everything fetch-related)
    auto* paramsBody = new QWidget(paramsPanel);
    paramsBody->setObjectName("DatasetSectionBody");
    auto* pbl = new QVBoxLayout(paramsBody);
    pbl->setContentsMargins(12, 12, 12, 12);
    pbl->setSpacing(8);

    auto mkLabel = [&](const QString& t, QWidget* parent) -> QLabel* {
        auto* l = new QLabel(t, parent);
        l->setObjectName("DatasetParamLabel");
        return l;
    };
    auto mkSpin = [&](int lo, int hi, int val) -> QSpinBox* {
        auto* s = new QSpinBox(paramsBody);
        s->setRange(lo, hi);
        s->setValue(val);
        s->setObjectName("DatasetSpin");
        return s;
    };

    m_tagInput = new QLineEdit(paramsBody);
    m_tagInput->setObjectName("SearchBar");
    m_tagInput->setPlaceholderText("e.g. nonomi (blue archive)");
    m_tagInput->setToolTip("Character or concept tag to query Danbooru for.");

    m_soloCheck = new QCheckBox("Append +solo", paramsBody);
    m_soloCheck->setObjectName("DatasetSoloCheck");
    m_soloCheck->setToolTip("Restrict the Danbooru query to solo posts of this character.\n"
                            "Helpful for cleaner clusters, but characters with few solo\n"
                            "posts will return less data - leave off if results are sparse.");

    m_charPagesSpin = mkSpin(1, 100, 15);
    m_globalPagesSpin = mkSpin(1, 200, 30);
    m_minCountSpin = mkSpin(1, 1000, 3);

    m_charPagesSpin->setToolTip("Pages of the character's posts to fetch (200 posts per page).\n"
                                "More pages = better PMI signal but slower.");
    m_globalPagesSpin->setToolTip("Pages of unrelated posts used to build the baseline tag\n"
                                  "distribution. Cached after the first fetch - only matters\n"
                                  "the first run or after clearing the global cache.");
    m_minCountSpin->setToolTip("Drop tags that appear in fewer than this many of the\n"
                               "character's posts. Live-applied - no re-fetch needed.");

    m_minPmiSlider = new QSlider(Qt::Horizontal, paramsBody);
    m_minPmiSlider->setObjectName("DatasetPmiSlider");
    m_minPmiSlider->setRange(kPmiSliderMin, kPmiSliderMax);
    m_minPmiSlider->setValue(kPmiSliderDefault);
    m_minPmiSlider->setToolTip("Pointwise mutual information threshold - higher values keep\n"
                               "only the most distinctive tags for this character. Live-applied.");

    m_minPmiValueLbl = new QLabel(paramsBody);
    m_minPmiValueLbl->setObjectName("DatasetParamValue");
    m_minPmiValueLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_minPmiValueLbl->setMinimumWidth(36);

    auto syncPmiLabel = [this]() {
        m_minPmiValueLbl->setText(QString::number(m_minPmiSlider->value() / 100.0, 'f', 2));
    };
    syncPmiLabel();

    auto* pmiRow = new QHBoxLayout;
    pmiRow->setContentsMargins(0, 0, 0, 0);
    pmiRow->setSpacing(6);
    pmiRow->addWidget(m_minPmiSlider, 1);
    pmiRow->addWidget(m_minPmiValueLbl);

    auto* grid = new QGridLayout;
    grid->setSpacing(4);
    grid->setColumnStretch(1, 1);
    int r = 0;
    grid->addWidget(mkLabel("Char pages", paramsBody), r, 0);
    grid->addWidget(m_charPagesSpin, r++, 1);
    grid->addWidget(mkLabel("Global pages", paramsBody), r, 0);
    grid->addWidget(m_globalPagesSpin, r++, 1);
    grid->addWidget(mkLabel("Min count", paramsBody), r, 0);
    grid->addWidget(m_minCountSpin, r++, 1);
    grid->addWidget(mkLabel("Min PMI", paramsBody), r, 0);
    grid->addLayout(pmiRow, r++, 1);

    m_fetchBtn = new QPushButton("Fetch", paramsBody);
    m_clearCacheBtn = new QPushButton("Clear global cache", paramsBody);
    m_fetchBtn->setObjectName("DatasetRunBtn");
    m_clearCacheBtn->setObjectName("EntryActionBtn");
    m_fetchBtn->setToolTip("Pull posts from Danbooru and rebuild the cluster.");
    m_clearCacheBtn->setToolTip("Delete the cached global tag distribution. The next fetch\n"
                                "will rebuild it from scratch.");

    pbl->addWidget(m_tagInput);
    pbl->addWidget(m_soloCheck);
    pbl->addLayout(grid);
    pbl->addWidget(m_fetchBtn);
    pbl->addWidget(m_clearCacheBtn);

    // Filter-section body - gets its own objectName so we can frame it with a
    // visible top + right border to set it apart from the params section above.
    auto* filterBody = new QWidget(paramsPanel);
    filterBody->setObjectName("DatasetFilterBody");
    filterBody->setAttribute(Qt::WA_StyledBackground, true);
    auto* fbl = new QVBoxLayout(filterBody);
    fbl->setContentsMargins(12, 12, 12, 12);
    fbl->setSpacing(8);

    m_blacklistRadio = new QRadioButton("Blacklist", filterBody);
    m_whitelistRadio = new QRadioButton("Whitelist", filterBody);
    m_blacklistRadio->setObjectName("DatasetFilterMode");
    m_whitelistRadio->setObjectName("DatasetFilterMode");
    m_blacklistRadio->setChecked(true);
    m_blacklistRadio->setToolTip("Drop any tag matching at least one rule below.");
    m_whitelistRadio->setToolTip("Keep only tags matching at least one rule below.");

    auto* modeRow = new QHBoxLayout;
    modeRow->setContentsMargins(0, 0, 0, 0);
    modeRow->setSpacing(8);
    modeRow->addWidget(m_blacklistRadio);
    modeRow->addWidget(m_whitelistRadio);
    modeRow->addStretch();

    m_filterEdit = new QPlainTextEdit(filterBody);
    m_filterEdit->setObjectName("DatasetExcludeEdit");
    m_filterEdit->setPlaceholderText("One rule per line - comma-separated facet names (AND).\n"
                                     "Multiple lines = OR.\n\n"
                                     "Example (whitelist):\n"
                                     "  eye, color >> (red eyes, blue eyes, ...)\n"
                                     "  hair, hairstyle >> (twintails, hair between eyes, ...)\n"
                                     "  hair, accessory >> (hair bow, ...)");
    m_filterEdit->setToolTip("Each line is a rule: every facet name listed (comma-separated)\n"
                             "must be present on the tag. Any rule matching = the tag matched\n"
                             "the filter. Live-applied - no re-fetch needed.");
    m_filterEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_filterEdit->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));

    m_saveFilterBtn = new QPushButton("Save filters", filterBody);
    m_saveFilterBtn->setObjectName("EntryActionBtn");
    m_saveFilterBtn->setToolTip("Persist the current rules to data/system/cluster_filters.fct.");
    m_filterStatusLbl = new QLabel(filterBody);
    m_filterStatusLbl->setObjectName("DatasetStatusLabel");

    fbl->addLayout(modeRow);
    fbl->addWidget(m_filterEdit, 1);
    fbl->addWidget(m_saveFilterBtn);
    fbl->addWidget(m_filterStatusLbl);

    // Distinct objectName so QSS skips the header's border-bottom; the
    // filter body owns the border-top instead, avoiding a doubled seam.
    auto* filterHeader = makeSectionHeader("FACET FILTER");
    filterHeader->setObjectName("DatasetFilterSectionHeader");

    pl->addWidget(makeSectionHeader("TAG"));
    pl->addWidget(paramsBody);
    pl->addWidget(filterHeader);
    pl->addWidget(filterBody, 1);

    // ---- Results panel (middle)
    auto* resultsPanel = new QWidget(this);
    auto* rl = new QVBoxLayout(resultsPanel);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);

    auto* resultsBody = new QWidget(resultsPanel);
    auto* rbl = new QVBoxLayout(resultsBody);
    rbl->setContentsMargins(12, 12, 12, 12);
    rbl->setSpacing(8);

    // emptyState owns the "no fetch yet" copy; this label only appears
    // once a fetch is in flight or produced a result count.
    m_statusLabel = new QLabel(resultsBody);
    m_statusLabel->setObjectName("DatasetStatusLabel");

    m_progressBar = new QProgressBar(resultsBody);
    m_progressBar->setObjectName("DatasetProgressBar");
    m_progressBar->setTextVisible(true);
    m_progressBar->setVisible(false);

    m_resultsContainer = new QWidget;
    m_resultsContainer->setObjectName("DatasetResultsList");
    m_resultsLayout = new QVBoxLayout(m_resultsContainer);
    m_resultsLayout->setContentsMargins(0, 0, 0, 0);
    m_resultsLayout->setSpacing(1);
    m_resultsLayout->addStretch();

    auto* scroll = new QScrollArea(resultsBody);
    scroll->setWidget(m_resultsContainer);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setObjectName("DatasetResultsScroll");
    scroll->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));
    m_resultsScroll = scroll;

    // Centered empty-state when there are no rows; stretch + AlignHCenter
    // pin it both axes.
    auto* emptyContainer = new QWidget(resultsBody);
    emptyContainer->setObjectName("DatasetEmptyContainer");
    auto* ecl = new QVBoxLayout(emptyContainer);
    ecl->setContentsMargins(0, 0, 0, 0);
    ecl->setSpacing(0);
    m_emptyStateLbl = new QLabel("Enter a tag and click Fetch.", emptyContainer);
    m_emptyStateLbl->setObjectName("DatasetEmptyState");
    m_emptyStateLbl->setAlignment(Qt::AlignCenter);
    ecl->addStretch();
    ecl->addWidget(m_emptyStateLbl, 0, Qt::AlignHCenter);
    ecl->addStretch();
    m_emptyState = emptyContainer;

    m_copyEdit = new QPlainTextEdit(resultsBody);
    m_copyEdit->setObjectName("DatasetCopyEdit");
    m_copyEdit->setReadOnly(true);
    m_copyEdit->setMaximumHeight(80);
    m_copyEdit->setPlaceholderText("Copy string will appear here after fetching...");
    m_copyEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_copyEdit->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));

    m_copyBtn = new QPushButton("Copy to Clipboard", resultsBody);
    m_copyBtn->setObjectName("DatasetRunBtn");
    m_copyBtn->setEnabled(false);

    rbl->addWidget(m_statusLabel);
    rbl->addWidget(m_progressBar);
    rbl->addWidget(scroll, 1);
    rbl->addWidget(emptyContainer, 1);
    rbl->addWidget(m_copyEdit);
    rbl->addWidget(m_copyBtn);

    rl->addWidget(makeSectionHeader("RESULTS"));
    rl->addWidget(resultsBody, 1);

    setResultsEmpty(true);

    // ---- Preview panel (right)
    auto* previewPanel = new QWidget(this);
    previewPanel->setObjectName("DatasetPreviewPanel");
    previewPanel->setAttribute(Qt::WA_StyledBackground, true);
    previewPanel->setFixedWidth(kPreviewPanelWidth);

    auto* prl = new QVBoxLayout(previewPanel);
    prl->setContentsMargins(0, 0, 0, 0);
    prl->setSpacing(0);

    auto* previewBody = new QWidget(previewPanel);
    auto* pvl = new QVBoxLayout(previewBody);
    pvl->setContentsMargins(12, 16, 12, 12);
    pvl->setSpacing(8);

    m_previewImage = new QLabel(previewBody);
    m_previewImage->setObjectName("DatasetPreviewImage");
    m_previewImage->setAttribute(Qt::WA_StyledBackground, true);
    m_previewImage->setAlignment(Qt::AlignCenter);
    m_previewImage->setMinimumHeight(220);
    m_previewImage->installEventFilter(this);
    m_previewImage->hide();

    m_previewStatus = new QLabel(previewBody);
    m_previewStatus->setObjectName("DatasetStatusLabel");
    m_previewStatus->setAlignment(Qt::AlignCenter);
    m_previewStatus->setWordWrap(true);
    m_previewStatus->hide();

    // Center the image vertically and horizontally in the available space.
    pvl->addStretch();
    pvl->addWidget(m_previewImage, 0, Qt::AlignHCenter);
    pvl->addWidget(m_previewStatus, 0, Qt::AlignHCenter);
    pvl->addStretch();

    prl->addWidget(makeSectionHeader("PREVIEW"));
    prl->addWidget(previewBody, 1);

    // ---- Root
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(paramsPanel);
    root->addWidget(resultsPanel, 1);
    root->addWidget(previewPanel);

    // ---- Wire
    connect(m_fetchBtn, &QPushButton::clicked, this, &TagClusterPage::onFetchClicked);
    connect(m_tagInput, &QLineEdit::returnPressed, this, &TagClusterPage::onFetchClicked);
    connect(m_clearCacheBtn, &QPushButton::clicked, this, [this]() {
        QFile::remove(cachePath());
        m_globalCounter.clear();
        m_globalTotal = 0;
        setStatus("Global cache cleared.");
    });
    connect(m_copyBtn, &QPushButton::clicked, this,
            [this]() { QApplication::clipboard()->setText(m_copyEdit->toPlainText()); });

    // +solo flips the Danbooru query - flag the cached data as stale until the
    // user re-fetches, since the underlying post population is now different.
    connect(m_soloCheck, &QCheckBox::toggled, this, [this](bool) { markStaleIfFetched(); });

    // Debounced recompute - bursts (slider drag, typing in the filter editor)
    // collapse into a single rebuild after the user pauses for ~150 ms.
    connect(m_minPmiSlider, &QSlider::valueChanged, this, [this, syncPmiLabel](int) {
        syncPmiLabel();
        scheduleRecompute();
    });
    connect(m_minCountSpin, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int) { scheduleRecompute(); });
    connect(m_filterEdit, &QPlainTextEdit::textChanged, this, [this]() { scheduleRecompute(); });
    connect(m_blacklistRadio, &QRadioButton::toggled, this, [this](bool) { scheduleRecompute(); });
    connect(m_whitelistRadio, &QRadioButton::toggled, this, [this](bool) { scheduleRecompute(); });

    connect(m_saveFilterBtn, &QPushButton::clicked, this, &TagClusterPage::saveFilters);

    // Boot: hydrate filters from disk if a file exists.
    loadFilters();
}

// ---- Fetch

void TagClusterPage::onFetchClicked()
{
    if (m_phase != Phase::Idle) return;

    const QString raw = m_tagInput->text().trimmed();
    if (raw.isEmpty()) return;

    QString tmp = raw;
    tmp.replace(' ', '_');
    m_targetTag = tmp;
    m_fetchedSolo = m_soloCheck->isChecked();

    // Clear previous results + char counters; preserve global cache.
    clearResultRows();
    m_copyEdit->clear();
    m_copyBtn->setEnabled(false);
    m_copyright.clear();
    m_charCounter.clear();
    m_copyrightCounter.clear();
    m_charTotal = 0;
    m_charDataReady = false;
    m_charPages = m_charPagesSpin->value();
    m_globalPages = m_globalPagesSpin->value();
    m_currentPage = 0;

    if (hasCachedGlobal() && m_globalCounter.isEmpty()) loadGlobalCache();

    if (!m_globalCounter.isEmpty()) {
        setStatus(QString("Global cache loaded (%1 tags). Fetching character posts...")
                      .arg(m_globalCounter.size()));
        m_phase = Phase::CharFetch;
    }
    else {
        m_globalCounter.clear();
        m_globalTotal = 0;
        setStatus("Building global tag distribution (this only runs once)...");
        m_phase = Phase::GlobalFetch;
    }

    setFetchRunning(true);
    if (m_emptyStateLbl) m_emptyStateLbl->setText("Fetching…");

    // Kick off the right-pane preview in parallel - independent of the fetch
    // pipeline, no need to block on it.
    fetchPreview(m_targetTag);

    fetchNextPage();
}

void TagClusterPage::fetchNextPage()
{
    QUrl url(DANBOORU_BASE);
    QUrlQuery q;
    q.addQueryItem("limit", "200");
    q.addQueryItem("page", QString::number(m_currentPage + 1));
    if (m_phase == Phase::CharFetch) {
        // +solo lives directly in the tag string - Danbooru treats space as AND.
        QString tags = m_targetTag;
        if (m_fetchedSolo) tags += " solo";
        q.addQueryItem("tags", tags);
    }
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "TagComposer/1.0");

    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            setStatus(QString("Network error: %1").arg(reply->errorString()));
            setFetchRunning(false);
            m_phase = Phase::Idle;
            if (m_emptyStateLbl) m_emptyStateLbl->setText("Network error - try again.");
            setResultsEmpty(true);
            return;
        }

        const QJsonArray posts = QJsonDocument::fromJson(reply->readAll()).array();

        if (!posts.isEmpty()) {
            processPage(posts);
            m_currentPage++;
        }

        const int total = (m_phase == Phase::GlobalFetch) ? m_globalPages : m_charPages;
        setProgress(qMin(m_currentPage, total), total);

        const bool exhausted = posts.isEmpty() || m_currentPage >= total;
        if (exhausted)
            onPhaseDone();
        else
            QTimer::singleShot(500, this, &TagClusterPage::fetchNextPage);
    });
}

void TagClusterPage::processPage(const QJsonArray& posts)
{
    for (const QJsonValue& v : posts) {
        const QJsonObject post = v.toObject();
        if (m_phase == Phase::GlobalFetch) {
            const QStringList tags =
                post["tag_string_general"].toString().split(' ', Qt::SkipEmptyParts);
            for (const QString& t : tags)
                m_globalCounter[t]++;
            m_globalTotal += tags.size();
        }
        else {
            const QStringList gen =
                post["tag_string_general"].toString().split(' ', Qt::SkipEmptyParts);
            for (const QString& t : gen)
                m_charCounter[t]++;
            m_charTotal += gen.size();

            const QStringList cop =
                post["tag_string_copyright"].toString().split(' ', Qt::SkipEmptyParts);
            for (const QString& c : cop)
                m_copyrightCounter[c]++;
        }
    }
}

void TagClusterPage::onPhaseDone()
{
    if (m_phase == Phase::GlobalFetch) {
        saveGlobalCache();
        m_currentPage = 0;
        m_phase = Phase::CharFetch;
        setStatus(QString("Global stats built (%1 tags). Fetching character posts...")
                      .arg(m_globalCounter.size()));
        QTimer::singleShot(500, this, &TagClusterPage::fetchNextPage);
    }
    else {
        m_phase = Phase::Idle;
        setFetchRunning(false);
        m_charDataReady = true;

        // Most common copyright (computed once per fetch - not part of the
        // recompute hot path).
        m_copyright = "No Copyright";
        int maxCop = 0;
        for (auto it = m_copyrightCounter.constBegin(); it != m_copyrightCounter.constEnd(); ++it)
            if (it.value() > maxCop) {
                maxCop = it.value();
                m_copyright = it.key();
            }

        recompute();
    }
}

// ---- Recompute (runs on every threshold/filter change)

void TagClusterPage::recompute()
{
    if (!m_charDataReady) return;

    const core::ClusterFilter filter = buildFilterFromEditor();

    const double minPmi = m_minPmiSlider->value() / 100.0;
    const int minCnt = m_minCountSpin->value();

    // Each candidate tag carries its own PMI (the value displayed in the
    // results) and a sort weight (PMI scaled by sqrt(freq) so common-and-
    // distinctive tags rank above rare-but-distinctive ones).
    struct Scored {
        QString tag;
        double pmi;
        double sortKey;
    };
    QList<Scored> scored;

    for (auto it = m_charCounter.constBegin(); it != m_charCounter.constEnd(); ++it) {
        const QString& tag = it.key();
        const int count = it.value();

        if (count < minCnt) continue;

        // FacetIndex keys on space form, but Danbooru tags use underscores.
        // Tags without a facet hit ClusterFilter::keep (kept in blacklist
        // mode, dropped in whitelist).
        const QString lookupTag = utils::normalizeTagInput(tag);
        const QList<QString> facets = m_facets ? m_facets->facetsFor(lookupTag) : QList<QString>{};
        if (!filter.keep(facets)) continue;

        const int gCount = m_globalCounter.value(tag, 0);
        if (gCount < 50) continue; // global rarity floor - keeps PMI stable

        // Pure MLE PMI - the gCount floor above is what protects the log()
        // from blowing up on rare/zero-count tags.
        const double p_tc = double(count) / double(m_charTotal);
        const double p_t = double(gCount) / double(m_globalTotal);
        const double pmi = std::log(p_tc / p_t);
        if (pmi < minPmi) continue;

        const double freq = double(count) / double(m_charTotal);
        scored.append({tag, pmi, pmi * std::sqrt(freq)});
    }

    // Sort by raw PMI so the on-screen order matches the displayed score
    // column (and the slider's threshold). The freq-weighted sortKey is kept
    // as a tiebreaker so equally distinctive tags are stably ordered.
    std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
        if (a.pmi != b.pmi) return a.pmi > b.pmi;
        return a.sortKey > b.sortKey;
    });

    // Direct delete (not deleteLater) so a fast recompute doesn't briefly
    // stack zombie widgets during the layout reflow.
    clearResultRows();

    for (int i = 0; i < scored.size(); ++i) {
        m_rows.append({nullptr, scored[i].tag, true});
        QWidget* row = makeResultRow(scored[i].tag, scored[i].pmi, i);
        m_rows[i].widget = row;
        m_resultsLayout->insertWidget(i, row);
    }

    setStatus(QString("Showing %1 tags (re-tweak any threshold to refine).").arg(scored.size()));
    m_copyBtn->setEnabled(!scored.isEmpty());
    if (scored.isEmpty() && m_emptyStateLbl)
        m_emptyStateLbl->setText("No tags matched the current filters.");
    setResultsEmpty(scored.isEmpty());
    rebuildCopyString();
}

void TagClusterPage::scheduleRecompute()
{
    if (!m_charDataReady) return;
    m_recomputeTimer->start(); // restarts if already running
}

void TagClusterPage::clearResultRows()
{
    while (m_resultsLayout->count() > 1) {
        QLayoutItem* item = m_resultsLayout->takeAt(0);
        if (QWidget* w = item->widget()) delete w;
        delete item;
    }
    m_rows.clear();
}

QWidget* TagClusterPage::makeResultRow(const QString& tag, double pmi, int idx)
{
    auto* row = new QWidget(m_resultsContainer);
    row->setObjectName("DatasetResultRow");

    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(6, 2, 6, 2);
    layout->setSpacing(6);

    auto* removeBtn = new QPushButton("×", row);
    removeBtn->setObjectName("TagRemoveBtn");
    removeBtn->setFixedSize(18, 18);
    removeBtn->setCursor(Qt::PointingHandCursor);

    // Tags are stored Danbooru-style ("aqua_eyes") for API + facet lookup, but
    // shown with spaces to match the rest of the app's UI.
    auto* tagLbl = new QLabel(utils::normalizeTagInput(tag), row);
    tagLbl->setObjectName("DatasetResultTag");

    // The user threshold is in PMI units, so the displayed value is the raw
    // PMI (not the freq-weighted sort key) - that way the slider directly
    // matches the score column.
    auto* pmiLbl = new QLabel(QString::number(pmi, 'f', 2), row);
    pmiLbl->setObjectName("DatasetResultScore");
    pmiLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    pmiLbl->setFixedWidth(48);
    pmiLbl->setToolTip("PMI - pointwise mutual information vs. the global tag distribution.");

    layout->addWidget(removeBtn);
    layout->addWidget(tagLbl, 1);
    layout->addWidget(pmiLbl);

    connect(removeBtn, &QPushButton::clicked, this, [this, idx]() {
        if (idx < 0 || idx >= m_rows.size()) return;
        m_rows[idx].included = false;
        m_rows[idx].widget->setVisible(false);
        rebuildCopyString();
    });

    installRowContextMenu(row, tag);
    installRowContextMenu(tagLbl, tag);

    return row;
}

void TagClusterPage::installRowContextMenu(QWidget* w, const QString& tag)
{
    // Same wiki / facet-editor / quick-add menu as the composer.
    const QString wikiTag = utils::normalizeTagInput(tag);
    w->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(w, &QWidget::customContextMenuRequested, this, [this, wikiTag](const QPoint&) {
        QMenu menu;
        QAction* wikiAct = menu.addAction("Wiki");
        QAction* facetAct = menu.addAction("Edit facets");

        QHash<QAction*, QString> quickFacetActs;
        const QList<QPair<QString, QString>> entries{
            {"character", m_quickCharFacet},
            {"copyright", m_quickCopyFacet},
            {"trigger word", m_quickTriggerFacet},
            {"style", m_quickStyleFacet},
        };
        bool any = false;
        for (const auto& e : entries)
            if (!e.second.isEmpty()) {
                any = true;
                break;
            }
        if (any) menu.addSeparator();
        for (const auto& e : entries) {
            if (e.second.isEmpty()) continue;
            QAction* a = menu.addAction(QString("Quick add as %1 (%2)").arg(e.first, e.second));
            quickFacetActs.insert(a, e.second);
        }

        QAction* chosen = menu.exec(QCursor::pos());
        if (chosen == wikiAct)
            emit wikiRequested(wikiTag);
        else if (chosen == facetAct)
            emit facetEditorRequested(wikiTag);
        else if (chosen && quickFacetActs.contains(chosen))
            emit quickFacetRequested(wikiTag, quickFacetActs.value(chosen));
    });
}

void TagClusterPage::setQuickFacets(const QString& character, const QString& copyright,
                                    const QString& triggerWord, const QString& style)
{
    m_quickCharFacet = character;
    m_quickCopyFacet = copyright;
    m_quickTriggerFacet = triggerWord;
    m_quickStyleFacet = style;
}

void TagClusterPage::rebuildCopyString()
{
    QStringList parts;
    parts << fmtTag(m_targetTag);
    if (m_copyright != "No Copyright") parts << fmtTag(m_copyright);
    for (const auto& row : m_rows)
        if (row.included) parts << fmtTag(row.tag);
    m_copyEdit->setPlainText(parts.join(", "));
}

// ---- Filter persistence

core::ClusterFilter TagClusterPage::buildFilterFromEditor() const
{
    core::ClusterFilter f;
    f.mode = m_whitelistRadio->isChecked() ? core::ClusterFilter::Mode::Whitelist
                                           : core::ClusterFilter::Mode::Blacklist;

    for (const QString& rawLine : m_filterEdit->toPlainText().split('\n')) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;

        QList<QString> facets;
        for (const QString& tok : line.split(',', Qt::SkipEmptyParts))
            if (const QString t = tok.trimmed(); !t.isEmpty()) facets << t;
        if (!facets.isEmpty()) f.rules << facets;
    }
    return f;
}

void TagClusterPage::loadFilters()
{
    const QString path = BASE_PATH + "/" + CLUSTER_FILTERS_PATH;
    if (!QFile::exists(path)) return;

    const core::ClusterFilter f = core::ClusterFilter::loadFromFile(path);

    if (f.mode == core::ClusterFilter::Mode::Whitelist)
        m_whitelistRadio->setChecked(true);
    else
        m_blacklistRadio->setChecked(true);

    QStringList lines;
    for (const QList<QString>& rule : f.rules)
        lines << rule.join(", ");
    m_filterEdit->setPlainText(lines.join('\n'));
    m_filterStatusLbl->setText(QString("Loaded %1 rules.").arg(f.rules.size()));
}

void TagClusterPage::saveFilters()
{
    const QString path = BASE_PATH + "/" + CLUSTER_FILTERS_PATH;
    QDir().mkpath(BASE_PATH + "/data/system");

    const core::ClusterFilter f = buildFilterFromEditor();
    f.saveToFile(path);
    m_filterStatusLbl->setText(QString("Saved %1 rules.").arg(f.rules.size()));
}

// ---- Global cache

QString TagClusterPage::cachePath() const
{
    return BASE_PATH + "/" + BOORU_CACHE_PATH;
}

bool TagClusterPage::hasCachedGlobal() const
{
    return QFile::exists(cachePath());
}

void TagClusterPage::loadGlobalCache()
{
    QFile f(cachePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    m_globalTotal = root["total"].toInteger();
    m_globalCounter.clear();
    const QJsonObject tags = root["tags"].toObject();
    for (auto it = tags.constBegin(); it != tags.constEnd(); ++it)
        m_globalCounter[it.key()] = it.value().toInt();
}

void TagClusterPage::saveGlobalCache()
{
    QDir().mkpath(BASE_PATH + "/data/system");
    QFile f(cachePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    QJsonObject tags;
    for (auto it = m_globalCounter.constBegin(); it != m_globalCounter.constEnd(); ++it)
        tags[it.key()] = it.value();
    QJsonObject root;
    root["total"] = m_globalTotal;
    root["tags"] = tags;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

// ---- Preview chain (mirrors FacetEditorPage::fetchPreview)

void TagClusterPage::clearPreview()
{
    m_previewPostId = -1;
    m_previewImage->clear();
    m_previewImage->hide();
    m_previewImage->setCursor(Qt::ArrowCursor);
    m_previewStatus->clear();
    m_previewStatus->hide();
}

void TagClusterPage::fetchPreview(const QString& tag)
{
    clearPreview();
    m_previewForTag = tag;

    m_previewStatus->show();
    m_previewStatus->setText("Loading…");

    const QString slug = tagToApiSlug(tag);
    const QByteArray enc = QUrl::toPercentEncoding(slug);
    QUrl url(
        QString("https://danbooru.donmai.us/wiki_pages/%1.json").arg(QString::fromLatin1(enc)));

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "TagComposer/1.0");
    req.setRawHeader("Accept", "application/json");

    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, tag, reply]() {
        reply->deleteLater();
        if (tag != m_previewForTag) return;

        if (reply->error() != QNetworkReply::NoError) {
            fetchFirstPostByTag(tag);
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            fetchFirstPostByTag(tag);
            return;
        }

        const QString body = doc.object().value("body").toString();
        static const QRegularExpression postRe(R"(!post\s+#(\d+))");
        const auto m = postRe.match(body);
        if (m.hasMatch())
            fetchPostById(tag, m.captured(1).toInt());
        else
            fetchFirstPostByTag(tag);
    });
}

void TagClusterPage::fetchPostById(const QString& tag, int postId)
{
    QUrl url(QString("https://danbooru.donmai.us/posts/%1.json").arg(postId));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "TagComposer/1.0");
    req.setRawHeader("Accept", "application/json");

    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, tag, postId, reply]() {
        reply->deleteLater();
        if (tag != m_previewForTag) return;

        if (reply->error() != QNetworkReply::NoError) {
            fetchFirstPostByTag(tag);
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            fetchFirstPostByTag(tag);
            return;
        }

        const QJsonObject post = doc.object();
        QString imgUrl = post.value("large_file_url").toString();
        if (imgUrl.isEmpty()) imgUrl = post.value("preview_file_url").toString();
        if (imgUrl.isEmpty()) {
            fetchFirstPostByTag(tag);
            return;
        }
        m_previewPostId = postId;
        fetchPreviewImage(tag, imgUrl);
    });
}

void TagClusterPage::fetchFirstPostByTag(const QString& tag)
{
    const QString slug = tagToApiSlug(tag);
    QUrl url("https://danbooru.donmai.us/posts.json");
    QUrlQuery q;
    q.addQueryItem("tags", slug);
    q.addQueryItem("limit", "1");
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "TagComposer/1.0");
    req.setRawHeader("Accept", "application/json");

    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, tag, reply]() {
        reply->deleteLater();
        if (tag != m_previewForTag) return;

        if (reply->error() != QNetworkReply::NoError) {
            m_previewStatus->show();
            m_previewStatus->setText("(no preview)");
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isArray() || doc.array().isEmpty()) {
            m_previewStatus->show();
            m_previewStatus->setText("(no posts)");
            return;
        }
        const QJsonObject post = doc.array().first().toObject();
        QString imgUrl = post.value("large_file_url").toString();
        if (imgUrl.isEmpty()) imgUrl = post.value("preview_file_url").toString();
        if (imgUrl.isEmpty()) {
            m_previewStatus->show();
            m_previewStatus->setText("(no preview)");
            return;
        }
        m_previewPostId = post.value("id").toInt();
        fetchPreviewImage(tag, imgUrl);
    });
}

void TagClusterPage::fetchPreviewImage(const QString& tag, const QString& imageUrl)
{
    QNetworkRequest req((QUrl(imageUrl)));
    req.setHeader(QNetworkRequest::UserAgentHeader, "TagComposer/1.0");

    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, tag, reply]() {
        reply->deleteLater();
        if (tag != m_previewForTag) return;

        if (reply->error() != QNetworkReply::NoError) {
            m_previewStatus->show();
            m_previewStatus->setText("(image fetch failed)");
            return;
        }
        QPixmap pix;
        if (!pix.loadFromData(reply->readAll()) || pix.isNull()) {
            m_previewStatus->show();
            m_previewStatus->setText("(image decode failed)");
            return;
        }
        setPreviewPixmap(pix);
    });
}

void TagClusterPage::setPreviewPixmap(const QPixmap& pix)
{
    m_previewStatus->hide();
    m_previewStatus->clear();

    constexpr qreal kRad = 6.0;
    const QPixmap scaled =
        pix.scaled(kPreviewMaxW, kPreviewMaxH, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QPixmap rounded(scaled.size());
    rounded.fill(Qt::transparent);
    {
        QPainter p(&rounded);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        QPainterPath path;
        path.addRoundedRect(QRectF(rounded.rect()), kRad, kRad);
        p.setClipPath(path);
        p.drawPixmap(0, 0, scaled);
    }

    m_previewImage->setPixmap(rounded);
    m_previewImage->show();
    m_previewImage->setCursor(m_previewPostId > 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

bool TagClusterPage::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == m_previewImage && ev->type() == QEvent::MouseButtonRelease && m_previewPostId > 0) {
        auto* me = static_cast<QMouseEvent*>(ev);
        if (me->button() == Qt::LeftButton && m_previewImage->rect().contains(me->pos())) {
            QDesktopServices::openUrl(
                QUrl(QString("https://danbooru.donmai.us/posts/%1").arg(m_previewPostId)));
            return true;
        }
    }
    return QWidget::eventFilter(obj, ev);
}

// ---- UI helpers

void TagClusterPage::setStatus(const QString& msg)
{
    m_statusLabel->setText(msg);
}

void TagClusterPage::setProgress(int cur, int total)
{
    if (total <= 0) {
        m_progressBar->setVisible(false);
        return;
    }
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, total);
    m_progressBar->setValue(cur);
    m_progressBar->setFormat(QString("%1 / %2 pages").arg(cur).arg(total));
}

void TagClusterPage::setFetchRunning(bool on)
{
    m_fetchBtn->setEnabled(!on);
    m_tagInput->setEnabled(!on);
    m_soloCheck->setEnabled(!on);
    m_charPagesSpin->setEnabled(!on);
    m_globalPagesSpin->setEnabled(!on);
    m_progressBar->setVisible(on);
    if (!on) m_progressBar->setVisible(false);
}

void TagClusterPage::markStaleIfFetched()
{
    if (!m_charDataReady) return;
    m_charDataReady = false;
    setStatus("Parameters changed - click Fetch to refresh.");

    clearResultRows();
    m_copyEdit->clear();
    m_copyBtn->setEnabled(false);
    if (m_emptyStateLbl) m_emptyStateLbl->setText("Re-fetch to refresh.");
    setResultsEmpty(true);
}

void TagClusterPage::setResultsEmpty(bool empty)
{
    if (m_emptyState) m_emptyState->setVisible(empty);
    if (m_resultsScroll) m_resultsScroll->setVisible(!empty);
}

} // namespace gui
