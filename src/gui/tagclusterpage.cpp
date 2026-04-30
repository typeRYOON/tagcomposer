#include <gui/tagclusterpage.h>
#include <gui/widgets/appscrollbar.h>
#include <utils/appconfig.h>
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
#include <cmath>

using namespace utils;

namespace gui {

static const QString DANBOORU_BASE = "https://danbooru.donmai.us/posts.json";

static const QStringList DEFAULT_EXCLUDE = {
    "1girl", "solo", "looking_at_viewer", "smile", "open_mouth",
    "simple_background", "multiple_girls", "thighhighs", "ass",
    "large_breasts", "breasts", "bare_shoulders", "skirt"
};

static QString fmtTag(const QString& tag)
{
    return QString(tag).replace('_', ' ').replace('(', "\\(").replace(')', "\\)");
}

TagClusterPage::TagClusterPage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("TagClusterPage");

    m_nam = new QNetworkAccessManager(this);

    // ── Params panel ──────────────────────────────────────────────────────────
    auto* paramsPanel = new QWidget(this);
    paramsPanel->setObjectName("DatasetParamsPanel");
    paramsPanel->setAttribute(Qt::WA_StyledBackground, true);
    paramsPanel->setFixedWidth(250);

    auto* pl = new QVBoxLayout(paramsPanel);
    pl->setContentsMargins(10, 10, 10, 10);
    pl->setSpacing(6);

    auto mkLabel = [&](const QString& t) -> QLabel* {
        auto* l = new QLabel(t, paramsPanel);
        l->setObjectName("DatasetParamLabel");
        return l;
    };
    auto mkSpin = [&](int lo, int hi, int val) -> QSpinBox* {
        auto* s = new QSpinBox(paramsPanel);
        s->setRange(lo, hi); s->setValue(val);
        s->setObjectName("DatasetSpin");
        return s;
    };
    auto mkDSpin = [&](double lo, double hi, double step, double val, int dec = 3) -> QDoubleSpinBox* {
        auto* s = new QDoubleSpinBox(paramsPanel);
        s->setRange(lo, hi); s->setSingleStep(step);
        s->setDecimals(dec); s->setValue(val);
        s->setObjectName("DatasetSpin");
        return s;
    };

    m_tagInput = new QLineEdit(paramsPanel);
    m_tagInput->setObjectName("SearchBar");
    m_tagInput->setPlaceholderText("e.g. hatsune miku");

    m_charPagesSpin   = mkSpin(1, 100, 15);
    m_globalPagesSpin = mkSpin(1, 200, 30);
    m_topNSpin        = mkSpin(5, 200, 30);
    m_minFreqSpin     = mkDSpin(0.0, 1.0, 0.001, 0.005);
    m_minPmiSpin      = mkDSpin(0.0, 10.0, 0.05, 0.2, 2);
    m_alphaSpin       = mkDSpin(0.0, 10.0, 0.1,  1.0, 2);

    auto* grid = new QGridLayout;
    grid->setSpacing(4);
    grid->setColumnStretch(1, 1);
    int r = 0;
    grid->addWidget(mkLabel("Char pages"),   r, 0); grid->addWidget(m_charPagesSpin,   r++, 1);
    grid->addWidget(mkLabel("Global pages"), r, 0); grid->addWidget(m_globalPagesSpin, r++, 1);
    grid->addWidget(mkLabel("Top N"),        r, 0); grid->addWidget(m_topNSpin,        r++, 1);
    grid->addWidget(mkLabel("Min freq"),     r, 0); grid->addWidget(m_minFreqSpin,     r++, 1);
    grid->addWidget(mkLabel("Min PMI"),      r, 0); grid->addWidget(m_minPmiSpin,      r++, 1);
    grid->addWidget(mkLabel("Alpha"),        r, 0); grid->addWidget(m_alphaSpin,       r++, 1);

    m_excludeEdit = new QPlainTextEdit(paramsPanel);
    m_excludeEdit->setObjectName("DatasetExcludeEdit");
    m_excludeEdit->setPlainText(DEFAULT_EXCLUDE.join('\n'));
    m_excludeEdit->setMaximumHeight(200);
    m_excludeEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_excludeEdit->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));

    m_runBtn       = new QPushButton("Run",               paramsPanel);
    m_clearCacheBtn = new QPushButton("Clear global cache", paramsPanel);
    m_runBtn->setObjectName("DatasetRunBtn");
    m_clearCacheBtn->setObjectName("EntryActionBtn");

    pl->addWidget(mkLabel("Tag"));
    pl->addWidget(m_tagInput);
    pl->addLayout(grid);
    pl->addWidget(mkLabel("Excluded tags (one per line)"));
    pl->addWidget(m_excludeEdit);
    pl->addWidget(m_runBtn);
    pl->addWidget(m_clearCacheBtn);
    pl->addStretch();

    // ── Results panel ─────────────────────────────────────────────────────────
    auto* resultsPanel = new QWidget(this);
    auto* rl = new QVBoxLayout(resultsPanel);
    rl->setContentsMargins(8, 8, 8, 8);
    rl->setSpacing(6);

    m_statusLabel = new QLabel("Enter a tag and click Run.", resultsPanel);
    m_statusLabel->setObjectName("DatasetStatusLabel");

    m_progressBar = new QProgressBar(resultsPanel);
    m_progressBar->setObjectName("DatasetProgressBar");
    m_progressBar->setTextVisible(true);
    m_progressBar->setVisible(false);

    m_resultsContainer = new QWidget;
    m_resultsContainer->setObjectName("DatasetResultsList");
    m_resultsLayout = new QVBoxLayout(m_resultsContainer);
    m_resultsLayout->setContentsMargins(0, 0, 0, 0);
    m_resultsLayout->setSpacing(1);
    m_resultsLayout->addStretch();

    auto* scroll = new QScrollArea(resultsPanel);
    scroll->setWidget(m_resultsContainer);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setObjectName("DatasetResultsScroll");

    m_copyEdit = new QPlainTextEdit(resultsPanel);
    m_copyEdit->setObjectName("DatasetCopyEdit");
    m_copyEdit->setReadOnly(true);
    m_copyEdit->setMaximumHeight(80);
    m_copyEdit->setPlaceholderText("Copy string will appear here after running...");

    m_copyBtn = new QPushButton("Copy to Clipboard", resultsPanel);
    m_copyBtn->setObjectName("DatasetRunBtn");
    m_copyBtn->setEnabled(false);

    rl->addWidget(m_statusLabel);
    rl->addWidget(m_progressBar);
    rl->addWidget(scroll, 1);
    rl->addWidget(m_copyEdit);
    rl->addWidget(m_copyBtn);

    // ── Root ──────────────────────────────────────────────────────────────────
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(paramsPanel);
    root->addWidget(resultsPanel, 1);

    connect(m_runBtn,        &QPushButton::clicked, this, &TagClusterPage::onRunClicked);
    connect(m_tagInput,      &QLineEdit::returnPressed, this, &TagClusterPage::onRunClicked);
    connect(m_clearCacheBtn, &QPushButton::clicked, this, [this]() {
        QFile::remove(cachePath());
        setStatus("Global cache cleared.");
    });
    connect(m_copyBtn, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_copyEdit->toPlainText());
    });
}

// ── Run ───────────────────────────────────────────────────────────────────────

void TagClusterPage::onRunClicked()
{
    if (m_phase != Phase::Idle) return;

    const QString raw = m_tagInput->text().trimmed();
    if (raw.isEmpty()) return;

    QString tmp = raw;
    tmp.replace(' ', '_');
    m_targetTag = tmp;

    // Clear previous results
    while (m_resultsLayout->count() > 1) {
        QLayoutItem* item = m_resultsLayout->takeAt(0);
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
    m_rows.clear();
    m_copyEdit->clear();
    m_copyBtn->setEnabled(false);
    m_copyright.clear();
    m_charCounter.clear();
    m_copyrightCounter.clear();
    m_charTotal   = 0;
    m_charPages   = m_charPagesSpin->value();
    m_globalPages = m_globalPagesSpin->value();
    m_currentPage = 0;

    if (hasCachedGlobal()) {
        loadGlobalCache();
        setStatus(QString("Global cache loaded (%1 tags). Fetching character posts...")
                  .arg(m_globalCounter.size()));
        m_phase = Phase::CharFetch;
    } else {
        m_globalCounter.clear();
        m_globalTotal = 0;
        setStatus("Building global tag distribution (this only runs once)...");
        m_phase = Phase::GlobalFetch;
    }

    setRunning(true);
    fetchNextPage();
}

// ── Fetch loop ────────────────────────────────────────────────────────────────

void TagClusterPage::fetchNextPage()
{
    QUrl url(DANBOORU_BASE);
    QUrlQuery q;
    q.addQueryItem("limit", "200");
    q.addQueryItem("page",  QString::number(m_currentPage + 1));
    if (m_phase == Phase::CharFetch)
        q.addQueryItem("tags", m_targetTag);
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "TagComposer/1.0");

    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            setStatus(QString("Network error: %1").arg(reply->errorString()));
            setRunning(false);
            m_phase = Phase::Idle;
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
            const QStringList tags = post["tag_string_general"].toString()
                                         .split(' ', Qt::SkipEmptyParts);
            for (const QString& t : tags) m_globalCounter[t]++;
            m_globalTotal += tags.size();
        } else {
            const QStringList gen = post["tag_string_general"].toString()
                                        .split(' ', Qt::SkipEmptyParts);
            for (const QString& t : gen) m_charCounter[t]++;
            m_charTotal += gen.size();

            const QStringList cop = post["tag_string_copyright"].toString()
                                        .split(' ', Qt::SkipEmptyParts);
            for (const QString& c : cop) m_copyrightCounter[c]++;
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
    } else {
        m_phase = Phase::Idle;
        setRunning(false);
        computeAndDisplay();
    }
}

// ── PMI + display ─────────────────────────────────────────────────────────────

void TagClusterPage::computeAndDisplay()
{
    // Most common copyright
    m_copyright = "No Copyright";
    int maxCop = 0;
    for (auto it = m_copyrightCounter.constBegin(); it != m_copyrightCounter.constEnd(); ++it)
        if (it.value() > maxCop) { maxCop = it.value(); m_copyright = it.key(); }

    // Excluded set
    QSet<QString> excluded;
    for (const QString& line : m_excludeEdit->toPlainText().split('\n', Qt::SkipEmptyParts))
        excluded.insert(line.trimmed());

    const double alpha    = m_alphaSpin->value();
    const double minFreq  = m_minFreqSpin->value();
    const double minPmi   = m_minPmiSpin->value();
    const int    topN     = m_topNSpin->value();
    const int    vocab    = m_globalCounter.size();

    QList<QPair<QString, double>> scored;
    for (auto it = m_charCounter.constBegin(); it != m_charCounter.constEnd(); ++it) {
        const QString& tag   = it.key();
        const int      count = it.value();

        const double freq = double(count) / double(m_charTotal);
        if (freq < minFreq)          continue;
        if (excluded.contains(tag))  continue;

        const int gCount = m_globalCounter.value(tag, 0);
        if (gCount < 50) continue;

        const double p_tc = (count  + alpha) / (double(m_charTotal)   + alpha * vocab);
        const double p_t  = (gCount + alpha) / (double(m_globalTotal)  + alpha * vocab);
        const double pmi  = std::log(p_tc / p_t);
        if (pmi < minPmi) continue;

        scored.append({ tag, pmi * std::sqrt(freq) });
    }

    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });
    if (scored.size() > topN) scored.resize(topN);

    // Rebuild result widgets
    while (m_resultsLayout->count() > 1) {
        QLayoutItem* item = m_resultsLayout->takeAt(0);
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
    m_rows.clear();

    for (int i = 0; i < scored.size(); ++i) {
        m_rows.append({ nullptr, scored[i].first, true });
        QWidget* row = makeResultRow(scored[i].first, scored[i].second, i);
        m_rows[i].widget = row;
        m_resultsLayout->insertWidget(i, row);
    }

    setStatus(QString("Done — %1 tags.").arg(scored.size()));
    m_copyBtn->setEnabled(!scored.isEmpty());
    rebuildCopyString();
}

QWidget* TagClusterPage::makeResultRow(const QString& tag, double score, int idx)
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

    auto* tagLbl = new QLabel(tag, row);
    tagLbl->setObjectName("DatasetResultTag");

    auto* scoreLbl = new QLabel(QString::number(score, 'f', 3), row);
    scoreLbl->setObjectName("DatasetResultScore");
    scoreLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    scoreLbl->setFixedWidth(50);

    layout->addWidget(removeBtn);
    layout->addWidget(tagLbl, 1);
    layout->addWidget(scoreLbl);

    connect(removeBtn, &QPushButton::clicked, this, [this, idx]() {
        if (idx < 0 || idx >= m_rows.size()) return;
        m_rows[idx].included = false;
        m_rows[idx].widget->setVisible(false);
        rebuildCopyString();
    });

    return row;
}

void TagClusterPage::rebuildCopyString()
{
    QStringList parts;
    parts << fmtTag(m_targetTag);
    if (m_copyright != "No Copyright")
        parts << fmtTag(m_copyright);
    for (const auto& row : m_rows)
        if (row.included) parts << fmtTag(row.tag);
    m_copyEdit->setPlainText(parts.join(", "));
}

// ── Cache ─────────────────────────────────────────────────────────────────────

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
    QDir().mkpath(BASE_PATH + "/data");
    QFile f(cachePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    QJsonObject tags;
    for (auto it = m_globalCounter.constBegin(); it != m_globalCounter.constEnd(); ++it)
        tags[it.key()] = it.value();
    QJsonObject root;
    root["total"] = m_globalTotal;
    root["tags"]  = tags;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

// ── UI helpers ────────────────────────────────────────────────────────────────

void TagClusterPage::setStatus(const QString& msg)
{
    m_statusLabel->setText(msg);
}

void TagClusterPage::setProgress(int cur, int total)
{
    if (total <= 0) { m_progressBar->setVisible(false); return; }
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, total);
    m_progressBar->setValue(cur);
    m_progressBar->setFormat(QString("%1 / %2 pages").arg(cur).arg(total));
}

void TagClusterPage::setRunning(bool on)
{
    m_runBtn->setEnabled(!on);
    m_tagInput->setEnabled(!on);
    m_charPagesSpin->setEnabled(!on);
    m_globalPagesSpin->setEnabled(!on);
    m_topNSpin->setEnabled(!on);
    m_minFreqSpin->setEnabled(!on);
    m_minPmiSpin->setEnabled(!on);
    m_alphaSpin->setEnabled(!on);
    m_progressBar->setVisible(on);
    if (!on) m_progressBar->setVisible(false);
}

} // namespace gui
