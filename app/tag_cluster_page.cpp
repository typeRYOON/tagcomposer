#include <app/tag_cluster_page.h>
#include <app/app_scroll_bar.h>
#include <app/icons.h>
#include <app/tag_preview_fetcher.h>
#include <app/tag_preview_popup.h>
#include <app/tag_search_bar.h>
#include <core/cluster_filter.h>
#include <core/entry.h>
#include <core/tag_facets.h>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace tc {

namespace cluster {

// Panel widths match the other dataset tabs so the columns line up when
// flipping between them.
constexpr int kParamsWidth = 380;
constexpr int kPreviewWidth = 420;
constexpr int kPreviewMaxWidth = kPreviewWidth - 24; // less the 12px body margins
constexpr int kPreviewMaxHeight = 560;
constexpr int kHeaderHeight = 50;

// Both sliders hold integers: a QDoubleSpinBox's keyboard stepping is awkward
// for a value that drives a live recompute.
constexpr int kPmiMin = 0;       // 0.00
constexpr int kPmiMax = 500;     // 5.00
constexpr int kPmiDefault = 50;  // 0.50
constexpr int kPctMin = 0;       // tenths of a percent
constexpr int kPctMax = 100;     // 10.0%
constexpr int kPctDefault = 10;  // 1.0%

constexpr int kRecomputeDebounceMs = 150;

// The marginal P(tag) denominator. It is a uniform log offset on every score,
// so an approximate corpus size is fine: it shifts where the slider sits, not
// the ranking.
constexpr double kDanbooruTotalPosts = 9000000.0;

// Below this, a tag is a typo or a one-off. Dropping them keeps log() well
// conditioned and keeps junk off the top of the list.
constexpr qint64 kGlobalCountFloor = 20;

QWidget* sectionHeader(const QString& title)
{
    auto* header = new QWidget;
    header->setObjectName(u"DatasetSectionHeader"_s);
    header->setAttribute(Qt::WA_StyledBackground, true);
    header->setFixedHeight(kHeaderHeight);

    auto* layout = new QHBoxLayout(header);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(8);

    auto* label = new QLabel(title, header);
    label->setObjectName(u"DatasetSectionTitle"_s);
    layout->addWidget(label);
    layout->addStretch();
    return header;
}

QLabel* paramLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(u"DatasetParamLabel"_s);
    return label;
}

// Danbooru wire form to prompt form: spaces, with parentheses escaped the way
// the composer writes them.
QString promptForm(const QString& tag)
{
    QString out = tag;
    out.replace(u'_', u' ');
    out.replace("("_L1, "\\("_L1);
    out.replace(")"_L1, "\\)"_L1);
    return out;
}

// Capitalises each word's first letter, leaving punctuation alone, so
// "taihou (azur lane)" becomes "Taihou (Azur Lane)".
QString titleCase(const QString& text)
{
    QString out;
    out.reserve(text.size());

    bool capitalise = true;
    for (const QChar c : text) {
        if (c.isSpace()) {
            capitalise = true;
            out.append(c);
            continue;
        }
        out.append(capitalise && c.isLetter() ? c.toUpper() : c);
        if (c.isLetter()) capitalise = false;
    }
    return out;
}

} // namespace cluster

using namespace cluster;

TagClusterPage::TagClusterPage(const TagFacets& facets, const QString& filtersPath,
                               QWidget* parent)
    : QWidget(parent), m_facets(&facets), m_filtersPath(filtersPath)
{
    setObjectName(u"TagClusterPage"_s);

    m_network = new QNetworkAccessManager(this);

    // The lookup is the fetcher's; this page only renders what it reports.
    m_preview = new TagPreviewFetcher(this);
    connect(m_preview, &TagPreviewFetcher::loading, this, [this](const QString&) {
        m_previewStatus->show();
        m_previewStatus->setText(u"Loading..."_s);
    });
    connect(m_preview, &TagPreviewFetcher::imageReady, this,
            [this](const QString& tag, const QPixmap& image, int postId) {
                if (tag != m_previewForTag) return;
                m_previewPostId = postId;
                showPreview(image);
            });
    connect(m_preview, &TagPreviewFetcher::failed, this,
            [this](const QString& tag, const QString& reason) {
                if (tag != m_previewForTag) return;
                m_previewStatus->show();
                m_previewStatus->setText(reason);
            });

    m_recomputeTimer = new QTimer(this);
    m_recomputeTimer->setSingleShot(true);
    m_recomputeTimer->setInterval(kRecomputeDebounceMs);
    connect(m_recomputeTimer, &QTimer::timeout, this, &TagClusterPage::recompute);

    // ---- Left: what to fetch, and what to keep
    auto* params = new QWidget;
    params->setObjectName(u"DatasetParamsPanel"_s);
    params->setAttribute(Qt::WA_StyledBackground, true);
    params->setFixedWidth(kParamsWidth);

    auto* paramsBody = new QWidget;
    paramsBody->setObjectName(u"DatasetSectionBody"_s);
    auto* paramsLayout = new QVBoxLayout(paramsBody);
    paramsLayout->setContentsMargins(12, 12, 12, 12);
    paramsLayout->setSpacing(8);

    m_tagInput = new QLineEdit;
    m_tagInput->setObjectName(u"SearchBar"_s);
    m_tagInput->setPlaceholderText(u"e.g. nonomi (blue archive)"_s);
    m_tagInput->setToolTip(u"Character or concept tag to query Danbooru for."_s);

    m_solo = new QCheckBox(u"Append +solo"_s);
    m_solo->setObjectName(u"DatasetSoloCheck"_s);
    m_solo->setToolTip(u"Restrict the query to solo posts, which gives a cleaner cluster.\n"
                       u"A character with few solo posts returns less to work with, so\n"
                       u"turn it off if the results come back sparse."_s);

    m_singleChar = new QCheckBox(u"Single character tag only"_s);
    m_singleChar->setObjectName(u"DatasetSoloCheck"_s);
    m_singleChar->setToolTip(u"Drop posts listing more than one character tag. Characters with\n"
                             u"alt-form tags otherwise pull in the other form's outfit."_s);

    m_pages = new QSpinBox;
    m_pages->setObjectName(u"DatasetSpin"_s);
    m_pages->setRange(1, 100);
    m_pages->setValue(15);
    m_pages->setToolTip(u"Pages of posts to fetch, 200 per page. More pages sharpen the\n"
                        u"PMI signal and take longer."_s);

    m_minPct = new QSlider(Qt::Horizontal);
    m_minPct->setObjectName(u"DatasetPmiSlider"_s);
    m_minPct->setRange(kPctMin, kPctMax);
    m_minPct->setValue(kPctDefault);
    m_minPct->setToolTip(u"Smallest share of the fetched posts a tag must appear in, applied\n"
                         u"before PMI. Resolved against this fetch's post count, so it\n"
                         u"scales with the page count."_s);

    m_minPctValue = new QLabel;
    m_minPctValue->setObjectName(u"DatasetParamValue"_s);
    m_minPctValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_minPctValue->setMinimumWidth(40);

    auto syncPct = [this]() {
        m_minPctValue->setText(QString::number(m_minPct->value() / 10.0, 'f', 1) + u"%"_s);
    };
    syncPct();

    auto* pctRow = new QHBoxLayout;
    pctRow->setContentsMargins(0, 0, 0, 0);
    pctRow->setSpacing(6);
    pctRow->addWidget(m_minPct, 1);
    pctRow->addWidget(m_minPctValue);

    m_minPmi = new QSlider(Qt::Horizontal);
    m_minPmi->setObjectName(u"DatasetPmiSlider"_s);
    m_minPmi->setRange(kPmiMin, kPmiMax);
    m_minPmi->setValue(kPmiDefault);
    m_minPmi->setToolTip(u"Pointwise mutual information floor. Higher keeps only what is\n"
                         u"distinctive to this character."_s);

    m_minPmiValue = new QLabel;
    m_minPmiValue->setObjectName(u"DatasetParamValue"_s);
    m_minPmiValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_minPmiValue->setMinimumWidth(40);

    auto syncPmi = [this]() {
        m_minPmiValue->setText(QString::number(m_minPmi->value() / 100.0, 'f', 2));
    };
    syncPmi();

    auto* pmiRow = new QHBoxLayout;
    pmiRow->setContentsMargins(0, 0, 0, 0);
    pmiRow->setSpacing(6);
    pmiRow->addWidget(m_minPmi, 1);
    pmiRow->addWidget(m_minPmiValue);

    auto* grid = new QGridLayout;
    grid->setSpacing(4);
    grid->setColumnStretch(1, 1);
    grid->addWidget(paramLabel(u"Pages"_s), 0, 0);
    grid->addWidget(m_pages, 0, 1);
    grid->addWidget(paramLabel(u"Min %"_s), 1, 0);
    grid->addLayout(pctRow, 1, 1);
    grid->addWidget(paramLabel(u"Min PMI"_s), 2, 0);
    grid->addLayout(pmiRow, 2, 1);

    m_fetchBtn = new QPushButton(u"Fetch"_s);
    m_fetchBtn->setObjectName(u"DatasetRunBtn"_s);
    m_fetchBtn->setCursor(Qt::PointingHandCursor);
    m_fetchBtn->setToolTip(u"Pull posts from Danbooru and build the cluster."_s);

    m_cancelBtn = new QPushButton(u"Cancel"_s);
    m_cancelBtn->setObjectName(u"DatasetCancelBtn"_s);
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setEnabled(false);
    m_cancelBtn->setToolTip(u"Stop here. What has already been pulled is kept and scored."_s);

    auto* fetchRow = new QHBoxLayout;
    fetchRow->setContentsMargins(0, 0, 0, 0);
    fetchRow->setSpacing(8);
    fetchRow->addWidget(m_fetchBtn, 1);
    fetchRow->addWidget(m_cancelBtn);

    paramsLayout->addWidget(m_tagInput);
    paramsLayout->addWidget(m_solo);
    paramsLayout->addWidget(m_singleChar);
    paramsLayout->addLayout(grid);
    paramsLayout->addLayout(fetchRow);

    // Its own object name so the stylesheet can give it a top border, setting
    // it apart from the params above.
    auto* filterBody = new QWidget;
    filterBody->setObjectName(u"DatasetFilterBody"_s);
    filterBody->setAttribute(Qt::WA_StyledBackground, true);
    auto* filterLayout = new QVBoxLayout(filterBody);
    filterLayout->setContentsMargins(12, 12, 12, 12);
    filterLayout->setSpacing(8);

    m_blacklist = new QRadioButton(u"Blacklist"_s);
    m_whitelist = new QRadioButton(u"Whitelist"_s);
    m_blacklist->setObjectName(u"DatasetFilterMode"_s);
    m_whitelist->setObjectName(u"DatasetFilterMode"_s);
    m_blacklist->setChecked(true);
    m_blacklist->setToolTip(u"Drop any tag matching at least one rule below."_s);
    m_whitelist->setToolTip(u"Keep only tags matching at least one rule below."_s);

    auto* modeRow = new QHBoxLayout;
    modeRow->setContentsMargins(0, 0, 0, 0);
    modeRow->setSpacing(8);
    modeRow->addWidget(m_blacklist);
    modeRow->addWidget(m_whitelist);
    modeRow->addStretch();

    m_filterEdit = new QPlainTextEdit;
    m_filterEdit->setObjectName(u"DatasetExcludeEdit"_s);
    m_filterEdit->setPlaceholderText(u"One rule per line - comma-separated facets (AND).\n"
                                     u"Several lines are OR. Prefix with - to negate.\n\n"
                                     u"Example (whitelist):\n"
                                     u"  eye, color\n"
                                     u"  hair, hairstyle\n"
                                     u"  -meta, -nsfw"_s);
    m_filterEdit->setToolTip(u"Every facet named on a line must be present for that rule to\n"
                             u"match, and any matching rule matches the filter. Applied live,\n"
                             u"with no re-fetch.\n\n"
                             u"A '-' token is a negation. Those run first whatever the mode is:\n"
                             u"a tag carrying one is dropped before the rules are read."_s);
    m_filterEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_filterEdit->setVerticalScrollBar(new AppScrollBar(Qt::Vertical));

    m_saveFilterBtn = new QPushButton(u"Save filters"_s);
    m_saveFilterBtn->setObjectName(u"EntryActionBtn"_s);
    m_saveFilterBtn->setCursor(Qt::PointingHandCursor);
    m_saveFilterBtn->setToolTip(u"Write these rules to system/cluster_filters.fct."_s);

    m_filterStatus = new QLabel;
    m_filterStatus->setObjectName(u"DatasetStatusLabel"_s);

    filterLayout->addLayout(modeRow);
    filterLayout->addWidget(m_filterEdit, 1);
    filterLayout->addWidget(m_saveFilterBtn);
    filterLayout->addWidget(m_filterStatus);

    // A separate name so the stylesheet skips this header's bottom border:
    // the filter body owns the seam, and two would draw it twice.
    QWidget* filterHeader = sectionHeader(u"FACET FILTER"_s);
    filterHeader->setObjectName(u"DatasetFilterSectionHeader"_s);

    auto* paramsColumn = new QVBoxLayout(params);
    paramsColumn->setContentsMargins(0, 0, 0, 0);
    paramsColumn->setSpacing(0);
    paramsColumn->addWidget(sectionHeader(u"TAG"_s));
    paramsColumn->addWidget(paramsBody);
    paramsColumn->addWidget(filterHeader);
    paramsColumn->addWidget(filterBody, 1);

    // ---- Middle: the ranked tags
    auto* results = new QWidget;
    auto* resultsBody = new QWidget;
    auto* resultsLayout = new QVBoxLayout(resultsBody);
    resultsLayout->setContentsMargins(12, 12, 12, 12);
    resultsLayout->setSpacing(8);

    m_status = new QLabel;
    m_status->setObjectName(u"DatasetStatusLabel"_s);

    m_progress = new QProgressBar;
    m_progress->setObjectName(u"DatasetProgressBar"_s);
    m_progress->setTextVisible(true);
    m_progress->setVisible(false);

    m_resultsContainer = new QWidget;
    m_resultsContainer->setObjectName(u"DatasetResultsList"_s);
    m_resultsLayout = new QVBoxLayout(m_resultsContainer);
    m_resultsLayout->setContentsMargins(0, 0, 0, 0);
    m_resultsLayout->setSpacing(1);
    m_resultsLayout->addStretch();

    auto* scroll = new QScrollArea;
    scroll->setObjectName(u"DatasetResultsScroll"_s);
    scroll->setWidget(m_resultsContainer);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setVerticalScrollBar(new AppScrollBar(Qt::Vertical));
    m_resultsScroll = scroll;

    // Swapped in for the scroll area when there is nothing to show, centred on
    // both axes by the stretches around it.
    auto* empty = new QWidget;
    empty->setObjectName(u"DatasetEmptyContainer"_s);
    auto* emptyLayout = new QVBoxLayout(empty);
    emptyLayout->setContentsMargins(0, 0, 0, 0);
    emptyLayout->setSpacing(0);

    m_emptyLabel = new QLabel(u"Enter a tag and click Fetch."_s, empty);
    m_emptyLabel->setObjectName(u"DatasetEmptyState"_s);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLayout->addStretch();
    emptyLayout->addWidget(m_emptyLabel, 0, Qt::AlignHCenter);
    emptyLayout->addStretch();
    m_emptyState = empty;

    m_copyEdit = new QPlainTextEdit;
    m_copyEdit->setObjectName(u"DatasetCopyEdit"_s);
    m_copyEdit->setReadOnly(true);
    m_copyEdit->setMaximumHeight(80);
    m_copyEdit->setPlaceholderText(u"The copy string appears here after fetching..."_s);
    m_copyEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_copyEdit->setVerticalScrollBar(new AppScrollBar(Qt::Vertical));

    m_copyBtn = new QPushButton(u"Copy to Clipboard"_s);
    m_copyBtn->setObjectName(u"DatasetRunBtn"_s);
    m_copyBtn->setCursor(Qt::PointingHandCursor);
    m_copyBtn->setEnabled(false);

    m_createEntryBtn = new QPushButton(u"Create entry"_s);
    m_createEntryBtn->setObjectName(u"DatasetRunBtn"_s);
    m_createEntryBtn->setCursor(Qt::PointingHandCursor);
    m_createEntryBtn->setEnabled(false);
    m_createEntryBtn->setToolTip(u"Make an entry titled after the queried tag, carrying the\n"
                                 u"visible result tags and the copyright."_s);

    auto* buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(8);
    buttonRow->addWidget(m_copyBtn, 1);
    buttonRow->addWidget(m_createEntryBtn, 1);

    resultsLayout->addWidget(m_status);
    resultsLayout->addWidget(m_progress);
    resultsLayout->addWidget(scroll, 1);
    resultsLayout->addWidget(empty, 1);
    resultsLayout->addWidget(m_copyEdit);
    resultsLayout->addLayout(buttonRow);

    auto* resultsColumn = new QVBoxLayout(results);
    resultsColumn->setContentsMargins(0, 0, 0, 0);
    resultsColumn->setSpacing(0);
    resultsColumn->addWidget(sectionHeader(u"RESULTS"_s));
    resultsColumn->addWidget(resultsBody, 1);

    setResultsEmpty(true);

    // ---- Right: what the tag looks like
    auto* previewPanel = new QWidget;
    previewPanel->setObjectName(u"DatasetPreviewPanel"_s);
    previewPanel->setAttribute(Qt::WA_StyledBackground, true);
    previewPanel->setFixedWidth(kPreviewWidth);

    auto* previewBody = new QWidget;
    auto* previewLayout = new QVBoxLayout(previewBody);
    previewLayout->setContentsMargins(12, 16, 12, 12);
    previewLayout->setSpacing(8);

    m_previewImage = new QLabel;
    m_previewImage->setObjectName(u"DatasetPreviewImage"_s);
    m_previewImage->setAttribute(Qt::WA_StyledBackground, true);
    m_previewImage->setAlignment(Qt::AlignCenter);
    m_previewImage->setMinimumHeight(220);
    m_previewImage->installEventFilter(this);
    m_previewImage->hide();

    m_previewStatus = new QLabel;
    m_previewStatus->setObjectName(u"DatasetStatusLabel"_s);
    m_previewStatus->setAlignment(Qt::AlignCenter);
    m_previewStatus->setWordWrap(true);
    m_previewStatus->hide();

    previewLayout->addStretch();
    previewLayout->addWidget(m_previewImage, 0, Qt::AlignHCenter);
    previewLayout->addWidget(m_previewStatus, 0, Qt::AlignHCenter);
    previewLayout->addStretch();

    auto* previewColumn = new QVBoxLayout(previewPanel);
    previewColumn->setContentsMargins(0, 0, 0, 0);
    previewColumn->setSpacing(0);
    previewColumn->addWidget(sectionHeader(u"PREVIEW"_s));
    previewColumn->addWidget(previewBody, 1);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(params);
    root->addWidget(results, 1);
    root->addWidget(previewPanel);

    // ---- Wiring
    connect(m_fetchBtn, &QPushButton::clicked, this, &TagClusterPage::startFetch);
    connect(m_cancelBtn, &QPushButton::clicked, this, &TagClusterPage::cancelFetch);
    connect(m_tagInput, &QLineEdit::returnPressed, this, &TagClusterPage::startFetch);

    connect(m_copyBtn, &QPushButton::clicked, this,
            [this]() { QApplication::clipboard()->setText(m_copyEdit->toPlainText()); });

    connect(m_createEntryBtn, &QPushButton::clicked, this, [this]() {
        const QString target = m_targetTag.trimmed();
        if (target.isEmpty()) return;

        QStringList tags{m_targetTag};
        if (!m_copyright.isEmpty() && m_copyright != u"No Copyright"_s) tags << m_copyright;
        for (const ResultRow& row : m_rows)
            if (row.included) tags << row.tag;

        emit createEntryRequested(titleCase(QString(target).replace(u'_', u' ')), tags);
    });

    // These two change which posts are fetched, so the cached sample no longer
    // answers the question being asked.
    connect(m_solo, &QCheckBox::toggled, this, [this](bool) { markStale(); });
    connect(m_singleChar, &QCheckBox::toggled, this, [this](bool) { markStale(); });

    connect(m_minPct, &QSlider::valueChanged, this, [this, syncPct](int) {
        syncPct();
        scheduleRecompute();
    });
    connect(m_minPmi, &QSlider::valueChanged, this, [this, syncPmi](int) {
        syncPmi();
        scheduleRecompute();
    });
    connect(m_filterEdit, &QPlainTextEdit::textChanged, this,
            &TagClusterPage::scheduleRecompute);
    connect(m_blacklist, &QRadioButton::toggled, this, [this](bool) { scheduleRecompute(); });
    connect(m_whitelist, &QRadioButton::toggled, this, [this](bool) { scheduleRecompute(); });
    connect(m_saveFilterBtn, &QPushButton::clicked, this, &TagClusterPage::saveFilters);

    loadFilters();
}

void TagClusterPage::setDanbooruIndex(const DanbooruIndex* index)
{
    m_danbooru = index;

    // Attached here rather than in the constructor because the index loads
    // after the page is built. It never changes again afterwards.
    if (index && !m_autocomplete) m_autocomplete = new TagLineAutocomplete(m_tagInput, index, this);

    // A fetch that finished before the index arrived scored nothing, so give
    // it the pass it could not have. A no-op before the first fetch.
    scheduleRecompute();
}

void TagClusterPage::setQuickFacets(const QString& character, const QString& copyright,
                                    const QString& triggerWord, const QString& style)
{
    m_quickCharacter = character;
    m_quickCopyright = copyright;
    m_quickTrigger = triggerWord;
    m_quickStyle = style;
}

void TagClusterPage::refreshFacets()
{
    scheduleRecompute();
}

TagPreviewPopup* TagClusterPage::previewPopup()
{
    if (!m_previewPopup) m_previewPopup = new TagPreviewPopup(this);
    return m_previewPopup;
}

void TagClusterPage::clearPreview()
{
    m_preview->cancel();
    m_previewPostId = -1;
    m_previewImage->clear();
    m_previewImage->hide();
    m_previewImage->setCursor(Qt::ArrowCursor);
    m_previewStatus->clear();
    m_previewStatus->hide();
}

void TagClusterPage::showPreview(const QPixmap& image)
{
    m_previewStatus->hide();
    m_previewStatus->clear();

    m_previewImage->setPixmap(roundedPreview(image, kPreviewMaxWidth, kPreviewMaxHeight));
    m_previewImage->show();
    m_previewImage->setCursor(m_previewPostId > 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

bool TagClusterPage::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_previewImage || event->type() != QEvent::MouseButtonRelease
        || m_previewPostId <= 0)
        return QWidget::eventFilter(watched, event);

    auto* mouse = static_cast<QMouseEvent*>(event);
    if (mouse->button() == Qt::LeftButton && m_previewImage->rect().contains(mouse->pos())) {
        QDesktopServices::openUrl(
            QUrl(u"https://danbooru.donmai.us/posts/%1"_s.arg(m_previewPostId)));
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void TagClusterPage::setStatus(const QString& message)
{
    m_status->setText(message);
}

void TagClusterPage::setProgress(int current, int total)
{
    if (total <= 0) {
        m_progress->setVisible(false);
        return;
    }
    m_progress->setVisible(true);
    m_progress->setRange(0, total);
    m_progress->setValue(current);
    m_progress->setFormat(u"%1 / %2 pages"_s.arg(current).arg(total));
}

void TagClusterPage::setFetchRunning(bool running)
{
    m_fetchBtn->setEnabled(!running);
    m_cancelBtn->setEnabled(running);
    m_tagInput->setEnabled(!running);
    m_solo->setEnabled(!running);
    m_singleChar->setEnabled(!running);
    m_pages->setEnabled(!running);
    m_progress->setVisible(running);
}

void TagClusterPage::setResultsEmpty(bool empty)
{
    m_emptyState->setVisible(empty);
    m_resultsScroll->setVisible(!empty);
}

} // namespace tc
