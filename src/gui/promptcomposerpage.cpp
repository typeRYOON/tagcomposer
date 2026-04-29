#include <gui/promptcomposerpage.h>
#include <gui/appscrollbar.h>
#include <model/entrymodel.h>
#include <utils/appconfig.h>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QMenu>
#include <QCursor>
#include <QGuiApplication>
#include <QClipboard>
#include <QRegularExpression>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QInputDialog>
#include <QScrollArea>
#include <QScrollBar>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QTimer>
#include <QDir>
#include <QDate>
#include <QDateTime>
#include <QFrame>
#include <QScreen>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QPropertyAnimation>
#include <QtConcurrent>
#include <algorithm>

using namespace core;
using namespace utils;

// ── CategoryNavPanel ──────────────────────────────────────────────────────────
// Floating top-right widget: a small handle that expands on hover to show
// a scrollable list of the currently visible group names. No Q_OBJECT needed.

namespace {

class CategoryNavPanel : public QWidget {
public:
    std::function<void(const QString&)> onCategoryClicked;

    explicit CategoryNavPanel(QWidget* parent = nullptr) : QWidget(parent)
    {
        setAttribute(Qt::WA_StyledBackground, true);
        setObjectName("CategoryNavPanel");
        setFixedWidth(130);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        m_handle = new QLabel("☰  Groups", this);
        m_handle->setObjectName("CategoryNavHandle");
        m_handle->setFixedHeight(26);
        m_handle->setAlignment(Qt::AlignCenter);
        root->addWidget(m_handle);

        m_listFrame = new QWidget(this);
        m_listFrame->setObjectName("CategoryNavList");
        m_listLayout = new QVBoxLayout(m_listFrame);
        m_listLayout->setContentsMargins(0, 2, 0, 2);
        m_listLayout->setSpacing(0);
        root->addWidget(m_listFrame);
        m_listFrame->setMaximumHeight(0);

        m_anim = new QPropertyAnimation(m_listFrame, "maximumHeight", this);
        m_anim->setEasingCurve(QEasingCurve::InOutQuad);
        m_anim->setDuration(160);
        // Resize panel as list height changes so the widget tracks content
        connect(m_anim, &QPropertyAnimation::valueChanged,
                m_listFrame, [this](const QVariant&) { adjustSize(); });
    }

    void updateCategories(const QStringList& displayNames)
    {
        while (m_listLayout->count()) {
            auto* item = m_listLayout->takeAt(0);
            if (auto* w = item->widget()) w->deleteLater();
            delete item;
        }

        for (const QString& name : displayNames) {
            auto* btn = new QPushButton(name, m_listFrame);
            btn->setObjectName("CategoryNavBtn");
            btn->setFixedHeight(24);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setFlat(true);
            connect(btn, &QPushButton::clicked, btn, [this, name]() {
                if (onCategoryClicked) onCategoryClicked(name);
            });
            m_listLayout->addWidget(btn);
        }

        m_fullHeight = displayNames.size() * 24 + 4;
        // If already expanded, update live
        if (m_listFrame->maximumHeight() > 0)
            m_listFrame->setMaximumHeight(m_fullHeight);
        adjustSize();
    }

protected:
    void enterEvent(QEnterEvent*) override
    {
        m_anim->stop();
        m_anim->setStartValue(m_listFrame->maximumHeight());
        m_anim->setEndValue(m_fullHeight);
        m_anim->start();
    }

    void leaveEvent(QEvent*) override
    {
        m_anim->stop();
        m_anim->setStartValue(m_listFrame->maximumHeight());
        m_anim->setEndValue(0);
        m_anim->start();
    }

private:
    QLabel*             m_handle;
    QWidget*            m_listFrame;
    QVBoxLayout*        m_listLayout;
    QPropertyAnimation* m_anim;
    int                 m_fullHeight = 0;
};

} // anonymous namespace

namespace gui {


// ── Helpers ───────────────────────────────────────────────────────────────────

static QString dotColorFor(RuleResult r)
{
    switch (r) {
    case RuleResult::Include:  return "#336633";
    case RuleResult::Injected: return "#44bb44";
    case RuleResult::Skipped:  return "#2a2a2a";
    case RuleResult::Replaced: return "#552222";
    case RuleResult::Flagged:  return "#886622";
    case RuleResult::NoFacets:    return "#334466";
    case RuleResult::Deactivated: return "#2a2a2a";
    }
    return "#444444";
}

// Resolve path pattern: {yyyy-MM-dd} → today's date formatted by Qt date spec
static QString resolveOutputPath(const QString& pattern)
{
    if (pattern.isEmpty()) return {};
    const QDate today = QDate::currentDate();
    QString result;
    result.reserve(pattern.size() + 20);
    for (int i = 0; i < pattern.size(); ) {
        if (pattern[i] == QLatin1Char('{')) {
            const int j = pattern.indexOf(QLatin1Char('}'), i + 1);
            if (j > i) {
                result += today.toString(pattern.mid(i + 1, j - i - 1));
                i = j + 1;
                continue;
            }
        }
        result += pattern[i++];
    }
    return result;
}

// ── ClickableLabel ────────────────────────────────────────────────────────────
// QLabel that stores a source pixmap (rescales on resize), a file path, and
// opens the file in the OS viewer on left-click.

class ClickableLabel : public QLabel {
public:
    explicit ClickableLabel(QWidget* parent = nullptr) : QLabel(parent) {
        setCursor(Qt::PointingHandCursor);
        setAlignment(Qt::AlignCenter);
        setAttribute(Qt::WA_StyledBackground, true);
    }
    void setFilePath(const QString& path) { m_path = path; }
    void setSourcePixmap(const QPixmap& pix) {
        m_src = pix;
        updateScaled();
    }
protected:
    void resizeEvent(QResizeEvent* e) override {
        QLabel::resizeEvent(e);
        updateScaled();
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && !m_path.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_path));
        QLabel::mousePressEvent(e);
    }
private:
    void updateScaled() {
        if (!m_src.isNull() && width() > 0 && height() > 0)
            setPixmap(m_src.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    QString m_path;
    QPixmap m_src;
};

// ── PreviewClickLabel ─────────────────────────────────────────────────────────

PreviewClickLabel::PreviewClickLabel(QWidget* parent) : QLabel(parent)
{
    setAttribute(Qt::WA_Hover);
    setAttribute(Qt::WA_StyledBackground);
    setCursor(Qt::PointingHandCursor);
}

void PreviewClickLabel::setStepText(const QString& text)
{
    if (m_stepText == text) return;
    m_stepText = text;
    update();
}

void PreviewClickLabel::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) emit clicked();
    QLabel::mousePressEvent(e);
}

void PreviewClickLabel::paintEvent(QPaintEvent* e)
{
    QLabel::paintEvent(e);
    if (m_stepText.isEmpty()) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Clip to the inner edge of the 1px border so the overlay never bleeds
    // outside the rounded frame (border-radius: 4px → inner radius ≈ 3px)
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 3.0, 3.0);
    p.setClipPath(clip);

    constexpr int barH = 20;
    p.fillRect(QRect(0, height() - barH, width(), barH), QColor(5, 5, 5, 200));

    p.setPen(QColor(160, 160, 160));
    QFont f;
    f.setPixelSize(11);
    p.setFont(f);
    p.drawText(QRect(0, height() - barH, width() - 6, barH),
               Qt::AlignRight | Qt::AlignVCenter, m_stepText);
}

// ── ScaledImageLabel (local to this TU) ──────────────────────────────────────
// QLabel that keeps a source pixmap and rescales it when resized.

class ScaledImageLabel : public QLabel {
public:
    explicit ScaledImageLabel(QWidget* parent = nullptr) : QLabel(parent) {
        setObjectName("PopoutImageLabel");
        setAlignment(Qt::AlignCenter);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMinimumSize(50, 50);
        setAttribute(Qt::WA_StyledBackground);
    }
    void setSourcePixmap(const QPixmap& pix) {
        m_src = pix;
        updateScaled();
    }
protected:
    void resizeEvent(QResizeEvent* e) override {
        QLabel::resizeEvent(e);
        updateScaled();
    }
private:
    void updateScaled() {
        if (!m_src.isNull() && width() > 0 && height() > 0)
            setPixmap(m_src.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    QPixmap m_src;
};


// ── PreviewPopoutWindow ───────────────────────────────────────────────────────
// Separate top-level window (Qt::Window) parented to PromptComposerPage so Qt
// handles cleanup.  No Q_OBJECT needed — all connections use lambdas.

class PreviewPopoutWindow : public QWidget {
public:
    explicit PreviewPopoutWindow(QWidget* parent = nullptr);
    void setImage(const QPixmap& pix);
    void setOutputFolder(const QString&) {}
    void setTempFolder(const QString& folder);

protected:
    void resizeEvent(QResizeEvent* e) override;
    void showEvent(QShowEvent* e) override;

private:
    void loadNewestTempImage();

    ScaledImageLabel*       m_imageLabel;
    ClickableLabel*         m_tempLabel;
    QFileSystemWatcher*     m_watcher;
    QTimer*                 m_debounce;
    QFutureWatcher<QImage>* m_loadWatcher;
    QString                 m_tempFolder;
    QString                 m_lastTempPath;
};

PreviewPopoutWindow::PreviewPopoutWindow(QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint)
{
    setObjectName("PreviewPopout");
    setWindowTitle("Preview");
    resize(900, 700);
    setMinimumSize(400, 300);
    setAttribute(Qt::WA_StyledBackground);

    m_imageLabel = new ScaledImageLabel(this);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(m_imageLabel);

    // Floating small label: bottom-left, shows newest temp-folder image
    m_tempLabel = new ClickableLabel(this);
    m_tempLabel->setObjectName("PopoutTempLabel");
    m_tempLabel->hide();

    m_watcher     = new QFileSystemWatcher(this);
    m_debounce    = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(300);
    m_loadWatcher = new QFutureWatcher<QImage>(this);

    connect(m_watcher,  &QFileSystemWatcher::directoryChanged,
            this, [this](const QString&) { m_debounce->start(); });
    connect(m_debounce, &QTimer::timeout,
            this, [this]() { loadNewestTempImage(); });
    connect(m_loadWatcher, &QFutureWatcher<QImage>::finished, this, [this]() {
        const QImage img = m_loadWatcher->result();
        if (img.isNull()) return;
        m_tempLabel->setSourcePixmap(QPixmap::fromImage(img));
        m_tempLabel->setFilePath(m_lastTempPath);
        m_tempLabel->show();
        m_tempLabel->raise();
    });
}

void PreviewPopoutWindow::setImage(const QPixmap& pix)
{
    m_imageLabel->setSourcePixmap(pix);
}

void PreviewPopoutWindow::setTempFolder(const QString& folder)
{
    if (m_tempFolder == folder) return;
    if (!m_watcher->directories().isEmpty())
        m_watcher->removePaths(m_watcher->directories());
    m_tempFolder = folder;
    if (!folder.isEmpty() && QDir(folder).exists())
        m_watcher->addPath(folder);
}

void PreviewPopoutWindow::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    constexpr int margin = 12;
    const int side = qBound(120, qMin(width(), height()) / 2, 800);
    m_tempLabel->setFixedSize(side, side);
    m_tempLabel->move(margin, height() - side - margin);
}

void PreviewPopoutWindow::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    loadNewestTempImage();
}

void PreviewPopoutWindow::loadNewestTempImage()
{
    if (m_tempFolder.isEmpty()) return;
    static const QStringList filters = {"*.png", "*.jpg", "*.jpeg", "*.webp"};
    const QFileInfoList files = QDir(m_tempFolder).entryInfoList(filters, QDir::Files);
    if (files.isEmpty()) return;

    const QFileInfo* newest = &files[0];
    for (const QFileInfo& fi : files)
        if (fi.lastModified() > newest->lastModified()) newest = &fi;

    if (m_loadWatcher->isRunning()) return;
    m_lastTempPath = newest->absoluteFilePath();
    m_loadWatcher->setFuture(QtConcurrent::run([path = m_lastTempPath]() -> QImage {
        return QImage(path);
    }));
}

// ── WorkflowDropList ──────────────────────────────────────────────────────────

WorkflowDropList::WorkflowDropList(QWidget* parent) : QListWidget(parent)
{
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DropOnly);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setContextMenuPolicy(Qt::CustomContextMenu);
}

void WorkflowDropList::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls()) {
        for (const QUrl& url : e->mimeData()->urls()) {
            if (url.toLocalFile().endsWith(".json", Qt::CaseInsensitive)) {
                e->acceptProposedAction();
                return;
            }
        }
    }
    e->ignore();
}

void WorkflowDropList::dragMoveEvent(QDragMoveEvent* e)
{
    e->acceptProposedAction();
}

void WorkflowDropList::dropEvent(QDropEvent* e)
{
    for (const QUrl& url : e->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (path.endsWith(".json", Qt::CaseInsensitive))
            emit fileDropped(path);
    }
    e->acceptProposedAction();
}

// ── StatesListWidget ──────────────────────────────────────────────────────────

bool StatesListWidget::isImagePath(const QString& path)
{
    const QString l = path.toLower();
    return l.endsWith(".jpg") || l.endsWith(".jpeg")
        || l.endsWith(".png") || l.endsWith(".webp");
}

StatesListWidget::StatesListWidget(QWidget* parent) : QListWidget(parent)
{
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DropOnly);
    setMouseTracking(true);
}

void StatesListWidget::dragEnterEvent(QDragEnterEvent* e)
{
    if (!e->mimeData()->hasUrls()) { e->ignore(); return; }
    for (const QUrl& u : e->mimeData()->urls())
        if (isImagePath(u.toLocalFile())) { e->acceptProposedAction(); return; }
    e->ignore();
}

void StatesListWidget::dragMoveEvent(QDragMoveEvent* e)
{
    if (itemAt(e->position().toPoint())) e->acceptProposedAction();
    else e->ignore();
}

void StatesListWidget::dropEvent(QDropEvent* e)
{
    QListWidgetItem* item = itemAt(e->position().toPoint());
    if (!item) { e->ignore(); return; }
    for (const QUrl& u : e->mimeData()->urls()) {
        const QString path = u.toLocalFile();
        if (isImagePath(path)) {
            emit imageDroppedOnRow(row(item), path);
            e->acceptProposedAction();
            return;
        }
    }
    e->ignore();
}

// ── Ctor ──────────────────────────────────────────────────────────────────────

PromptComposerPage::PromptComposerPage(
    PromptPipeline*      pipeline,
    RuleEngine*          rules,
    const TagGroupIndex& groups,
    QWidget*             parent)
    : QWidget(parent)
    , m_pipeline(pipeline)
    , m_rules(rules)
    , m_groups(groups)
{
    setObjectName("PromptComposerPage");
    setAttribute(Qt::WA_StyledBackground, true);

    // ── Search bar ────────────────────────────────────────────────────────────
    m_searchBar = new TagSearchBar(this);
    m_searchBar->setActiveTags(&m_activeTagSet);
    connect(m_searchBar, &TagSearchBar::queryChanged, this, [this](const QString& text) {
        m_filterQuery = text.trimmed();
        applyTagFilter();
    });
    connect(m_searchBar, &TagSearchBar::tagAdded, this, [this](const QString& tag) {
        if (!m_activeTagSet.contains(tag)) {
            m_activeTags << tag;
            m_activeTagSet.insert(tag);
            m_freezeNextRebuild = true;
            QMetaObject::invokeMethod(this, &PromptComposerPage::repush, Qt::QueuedConnection);
        }
    });

    // ── Main groups area ──────────────────────────────────────────────────────
    m_groupsContainer = new QWidget;
    m_groupsContainer->setObjectName("ComposerGroupsContainer");
    m_groupsLayout = new QVBoxLayout(m_groupsContainer);
    m_groupsLayout->setContentsMargins(12, 12, 12, 12);
    m_groupsLayout->setSpacing(2);
    m_groupsLayout->addStretch();

    m_groupsScroll = new ComposerScrollArea;
    auto* groupsScroll = m_groupsScroll;
    groupsScroll->setObjectName("ComposerScroll");
    groupsScroll->setWidget(m_groupsContainer);
    groupsScroll->setWidgetResizable(true);
    groupsScroll->setFrameShape(QFrame::NoFrame);
    groupsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    groupsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(
        groupsScroll,
        &ComposerScrollArea::runRequested,
        this,
        [this]() {
            emit runRequested(m_promptCountSpin->value());
        }
    );
    connect(
        groupsScroll, &ComposerScrollArea::interruptRequested,
        this, &PromptComposerPage::interruptRequested
    );
    connect(
        groupsScroll, &ComposerScrollArea::clearPendingRequested,
        this, &PromptComposerPage::clearPendingRequested
    );

    auto* emptyHint = new QLabel("Press \"Composer Toggle\" on an entry image\nto push its tags here.");
    emptyHint->setObjectName("ComposerEmptyHint");
    emptyHint->setAlignment(Qt::AlignCenter);

    m_mainStack = new QStackedWidget;
    m_mainStack->addWidget(emptyHint);    // 0
    m_mainStack->addWidget(groupsScroll); // 1

    // ── Center stacked layout (bg layer + content layer) ─────────────────────
    m_centerBg = new QWidget;
    m_centerBg->setObjectName("ComposerCenterBg");

    auto* contentWidget = new QWidget;
    auto* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(m_searchBar);
    contentLayout->addWidget(m_mainStack, 1);

    auto* mainArea = new QWidget;
    auto* centerStack = new QStackedLayout(mainArea);
    centerStack->setStackingMode(QStackedLayout::StackAll);
    centerStack->addWidget(m_centerBg);
    centerStack->addWidget(contentWidget);
    centerStack->setCurrentIndex(1);

    // ── Rule sidebar ──────────────────────────────────────────────────────────
    m_rulesContainer = new QWidget;
    m_rulesContainer->setObjectName("ComposerRulesContainer");
    m_rulesLayout = new QVBoxLayout(m_rulesContainer);
    m_rulesLayout->setContentsMargins(8, 8, 8, 8);
    m_rulesLayout->setSpacing(4);
    m_rulesLayout->addStretch();

    auto* rulesScroll = new QScrollArea;
    rulesScroll->setObjectName("ComposerRulesScroll");
    rulesScroll->setWidget(m_rulesContainer);
    rulesScroll->setWidgetResizable(true);
    rulesScroll->setFrameShape(QFrame::NoFrame);

    rulesScroll->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));
    rulesScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* rulesHeaderRow = new QWidget;
    rulesHeaderRow->setObjectName("ComposerHeaderRow");
    rulesHeaderRow->setAttribute(Qt::WA_StyledBackground, true);
    auto* rhrL = new QHBoxLayout(rulesHeaderRow);
    rhrL->setContentsMargins(12, 10, 10, 6);
    rhrL->setSpacing(4);

    auto* rulesHeaderLabel = new QLabel("RULES");
    rulesHeaderLabel->setObjectName("ComposerHeaderLabel");

    auto* rulesOpenBtn = new QPushButton("↗");
    rulesOpenBtn->setObjectName("SidebarBtn");
    rulesOpenBtn->setFixedSize(20, 20);
    rulesOpenBtn->setCursor(Qt::PointingHandCursor);
    rulesOpenBtn->setToolTip("Open rules.fct in editor");
    connect(rulesOpenBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(BASE_PATH + "/" + RULES_PATH));
    });

    auto* rulesReloadBtn = new QPushButton("↺");
    rulesReloadBtn->setObjectName("SidebarBtn");
    rulesReloadBtn->setFixedSize(20, 20);
    rulesReloadBtn->setCursor(Qt::PointingHandCursor);
    rulesReloadBtn->setToolTip("Reload rules from file");
    connect(rulesReloadBtn, &QPushButton::clicked,
            this, &PromptComposerPage::reloadRules);

    rhrL->addWidget(rulesHeaderLabel, 1);
    rhrL->addWidget(rulesOpenBtn);
    rhrL->addWidget(rulesReloadBtn);

    // ── Variable editor section ───────────────────────────────────────────────
    m_varsContainer = new QWidget;
    m_varsContainer->setObjectName("ComposerVarsContainer");
    m_varsLayout = new QVBoxLayout(m_varsContainer);
    m_varsLayout->setContentsMargins(8, 6, 8, 8);
    m_varsLayout->setSpacing(6);

    auto* varsSep = new QWidget;
    varsSep->setFixedHeight(1);
    varsSep->setStyleSheet("background:#1a1a1a;");

    auto* varsHeaderRow = new QWidget;
    varsHeaderRow->setObjectName("ComposerHeaderRow");
    varsHeaderRow->setAttribute(Qt::WA_StyledBackground, true);
    auto* vhrL = new QHBoxLayout(varsHeaderRow);
    vhrL->setContentsMargins(12, 8, 8, 6);
    vhrL->setSpacing(4);

    auto* varsHeaderLabel = new QLabel("VARIABLES");
    varsHeaderLabel->setObjectName("ComposerHeaderLabel");

    auto* varsOpenBtn = new QPushButton("↗");
    varsOpenBtn->setObjectName("SidebarBtn");
    varsOpenBtn->setFixedSize(20, 20);
    varsOpenBtn->setCursor(Qt::PointingHandCursor);
    varsOpenBtn->setToolTip("Open vars.fct in editor");
    connect(varsOpenBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(BASE_PATH + "/" + VARS_PATH));
    });

    auto* varsReloadBtn = new QPushButton("↺");
    varsReloadBtn->setObjectName("SidebarBtn");
    varsReloadBtn->setFixedSize(20, 20);
    varsReloadBtn->setCursor(Qt::PointingHandCursor);
    varsReloadBtn->setToolTip("Reload variables from file");
    connect(varsReloadBtn, &QPushButton::clicked,
            this, &PromptComposerPage::reloadVars);

    vhrL->addWidget(varsHeaderLabel, 1);
    vhrL->addWidget(varsOpenBtn);
    vhrL->addWidget(varsReloadBtn);

    // ── Workflow / States sidebar section ─────────────────────────────────────
    auto* wfSep = new QWidget;
    wfSep->setFixedHeight(1);
    wfSep->setStyleSheet("background:#1a1a1a;");

    auto* wfHeaderRow = new QWidget;
    wfHeaderRow->setObjectName("ComposerHeaderRow");
    wfHeaderRow->setAttribute(Qt::WA_StyledBackground, true);
    auto* wfHRL = new QHBoxLayout(wfHeaderRow);
    wfHRL->setContentsMargins(8, 6, 8, 4);
    wfHRL->setSpacing(2);

    auto* wfTabBtn = new QPushButton("WF");
    wfTabBtn->setObjectName("ComposerModeTab");
    wfTabBtn->setCheckable(true);
    wfTabBtn->setChecked(true);
    wfTabBtn->setCursor(Qt::PointingHandCursor);

    auto* statesTabBtn = new QPushButton("STATES");
    statesTabBtn->setObjectName("ComposerModeTab");
    statesTabBtn->setCheckable(true);
    statesTabBtn->setChecked(false);
    statesTabBtn->setCursor(Qt::PointingHandCursor);

    m_wfEditBtnRef = new QPushButton("↗");
    m_wfEditBtnRef->setObjectName("SidebarBtn");
    m_wfEditBtnRef->setFixedSize(20, 20);
    m_wfEditBtnRef->setCursor(Qt::PointingHandCursor);
    m_wfEditBtnRef->setToolTip("Workflow Variable Editor");
    connect(m_wfEditBtnRef, &QPushButton::clicked, this, [this]() {
        emit workflowEditorRequested();
    });

    m_saveStateBtn = new QPushButton("+");
    m_saveStateBtn->setObjectName("SidebarBtn");
    m_saveStateBtn->setFixedSize(20, 20);
    m_saveStateBtn->setCursor(Qt::PointingHandCursor);
    m_saveStateBtn->setToolTip("Save current state");
    m_saveStateBtn->setVisible(false);
    connect(m_saveStateBtn, &QPushButton::clicked,
            this, &PromptComposerPage::saveCurrentState);

    wfHRL->addWidget(wfTabBtn);
    wfHRL->addSpacing(4);
    wfHRL->addWidget(statesTabBtn);
    wfHRL->addStretch(1);
    wfHRL->addWidget(m_wfEditBtnRef);
    wfHRL->addWidget(m_saveStateBtn);

    m_wfList = new WorkflowDropList(this);
    m_wfList->setObjectName("WfList");
    m_wfList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_wfList->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));

    connect(m_wfList, &WorkflowDropList::fileDropped, this, [this](const QString& path) {
        if (!m_wfManager) return;
        for (const auto& wf : m_wfManager->files())
            if (wf.path == path) return;
        QFileInfo fi(path);
        const QString newId = QString::number(QDateTime::currentMSecsSinceEpoch());
        m_wfManager->files() << core::WorkflowFile{ newId, fi.completeBaseName(), path };
        if (m_wfManager->selectedIndex() < 0)
            m_wfManager->setSelectedIndex(0);
        m_wfManager->saveToFile(m_wfSavePath);
        rebuildWorkflowList();
    });

    connect(m_wfList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (!m_wfManager || item->data(Qt::UserRole).isNull()) return;
        m_wfManager->setSelectedIndex(item->data(Qt::UserRole).toInt());
        m_wfManager->saveToFile(m_wfSavePath);
        rebuildWorkflowList();
        emit workflowVarsChanged();
    });

    connect(m_wfList, &QListWidget::customContextMenuRequested, this,
        [this](const QPoint& pos) {
            if (!m_wfManager) return;
            QListWidgetItem* item = m_wfList->itemAt(pos);
            if (!item || item->data(Qt::UserRole).isNull()) return;
            const int idx = item->data(Qt::UserRole).toInt();
            if (idx < 0 || idx >= m_wfManager->files().size()) return;

            QMenu menu(this);
            menu.addAction("Open file", this, [this, idx]() {
                if (idx >= m_wfManager->files().size()) return;
                QDesktopServices::openUrl(
                    QUrl::fromLocalFile(m_wfManager->files()[idx].path));
            });
            menu.addSeparator();
            menu.addAction("Rename", this, [this, idx]() {
                if (idx >= m_wfManager->files().size()) return;
                bool ok;
                const QString name = QInputDialog::getText(
                    this, "Rename Workflow", "Name:",
                    QLineEdit::Normal, m_wfManager->files()[idx].name, &ok);
                if (!ok || name.trimmed().isEmpty()) return;
                m_wfManager->files()[idx].name = name.trimmed();
                m_wfManager->saveToFile(m_wfSavePath);
                rebuildWorkflowList();
            });
            menu.addAction("Remove", this, [this, idx]() {
                if (idx >= m_wfManager->files().size()) return;
                m_wfManager->files().removeAt(idx);
                int sel = m_wfManager->selectedIndex();
                const int sz = m_wfManager->files().size();
                if (sz == 0)      m_wfManager->setSelectedIndex(-1);
                else if (sel >= sz) m_wfManager->setSelectedIndex(sz - 1);
                m_wfManager->saveToFile(m_wfSavePath);
                rebuildWorkflowList();
            });
            menu.exec(m_wfList->mapToGlobal(pos));
        });

    rebuildWorkflowList();

    // ── States list ───────────────────────────────────────────────────────────
    m_statesList = new StatesListWidget;
    m_statesList->setObjectName("StatesList");
    m_statesList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_statesList->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));
    m_statesList->installEventFilter(this);

    connect(m_statesList, &StatesListWidget::imageDroppedOnRow,
            this, [this](int row, const QString& srcPath) {
        if (row < 0 || row >= m_stateManager.states().size()) return;
        core::SavedState& state = m_stateManager.states()[row];
        const QString ext      = QFileInfo(srcPath).suffix().toLower();
        const QString stateDir = m_statesDir + "/" + state.id;
        QDir().mkpath(stateDir);
        // Delete any existing preview regardless of its extension
        if (!state.previewImagePath.isEmpty() && QFile::exists(state.previewImagePath))
            QFile::remove(state.previewImagePath);
        const QString dest = stateDir + "/preview." + ext;
        if (!QFile::copy(srcPath, dest)) return;
        state.previewImagePath = dest;
        m_stateManager.saveToDir(m_statesDir);
        rebuildStatesList();
    });

    connect(m_statesList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        const int row = m_statesList->row(item);
        if (row >= 0 && row < m_stateManager.states().size()) {
            m_freezeNextRebuild = true;
            restoreState(m_stateManager.states()[row]);
        }
    });

    connect(m_statesList, &QListWidget::itemEntered, this, [this](QListWidgetItem* item) {
        showStatePreview(m_statesList->row(item));
    });

    m_statesList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_statesList, &QListWidget::customContextMenuRequested, this,
        [this](const QPoint& pos) {
            QListWidgetItem* item = m_statesList->itemAt(pos);
            if (!item) return;
            const int row = m_statesList->row(item);
            if (row < 0 || row >= m_stateManager.states().size()) return;

            QMenu menu;
            QAction* openAct   = menu.addAction("Open state file");
            menu.addSeparator();
            QAction* renameAct = menu.addAction("Rename");
            QAction* deleteAct = menu.addAction("Delete");
            QAction* chosen    = menu.exec(m_statesList->mapToGlobal(pos));

            if (chosen == openAct) {
                const QString statePath =
                    m_statesDir + "/" + m_stateManager.states()[row].id + "/state.json";
                QDesktopServices::openUrl(QUrl::fromLocalFile(statePath));
            } else if (chosen == renameAct) {
                bool ok;
                const QString name = QInputDialog::getText(
                    this, "Rename State", "Name:", QLineEdit::Normal,
                    m_stateManager.states()[row].name, &ok);
                if (!ok || name.trimmed().isEmpty()) return;
                m_stateManager.states()[row].name = name.trimmed();
                m_stateManager.saveToDir(m_statesDir);
                rebuildStatesList();
            } else if (chosen == deleteAct) {
                const QString stateDir =
                    m_statesDir + "/" + m_stateManager.states()[row].id;
                QDir(stateDir).removeRecursively();
                m_stateManager.states().removeAt(row);
                m_stateManager.saveToDir(m_statesDir);
                hideStatePreview();
                rebuildStatesList();
            }
        });

    // Tab toggle
    connect(wfTabBtn, &QPushButton::clicked, this,
        [this, wfTabBtn, statesTabBtn]() {
            wfTabBtn->setChecked(true);
            statesTabBtn->setChecked(false);
            m_wfStateStack->setCurrentIndex(0);
            m_wfEditBtnRef->setVisible(true);
            m_saveStateBtn->setVisible(false);
            hideStatePreview();
        });
    connect(statesTabBtn, &QPushButton::clicked, this,
        [this, wfTabBtn, statesTabBtn]() {
            statesTabBtn->setChecked(true);
            wfTabBtn->setChecked(false);
            m_wfStateStack->setCurrentIndex(1);
            m_wfEditBtnRef->setVisible(false);
            m_saveStateBtn->setVisible(true);
        });

    m_wfStateStack = new QStackedWidget;
    m_wfStateStack->addWidget(m_wfList);     // 0
    m_wfStateStack->addWidget(m_statesList); // 1
    m_wfStateStack->setFixedHeight(200);

    // Floating preview popup for state images
    m_statesPreviewPopup = new QLabel(this,
        Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    m_statesPreviewPopup->setAttribute(Qt::WA_ShowWithoutActivating);
    m_statesPreviewPopup->setAlignment(Qt::AlignCenter);
    m_statesPreviewPopup->setStyleSheet(
        "background:#0d0d0d; border: 1px solid #2a2a2a; padding: 4px;");
    m_statesPreviewPopup->hide();

    auto* sidebar = new QWidget;
    sidebar->setObjectName("ComposerSidebar");
    sidebar->setFixedWidth(280);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);
    sidebarLayout->addWidget(rulesHeaderRow);
    sidebarLayout->addWidget(rulesScroll, 1);
    sidebarLayout->addWidget(wfSep);
    sidebarLayout->addWidget(wfHeaderRow);
    sidebarLayout->addWidget(m_wfStateStack);
    sidebarLayout->addWidget(varsSep);
    sidebarLayout->addWidget(varsHeaderRow);
    sidebarLayout->addWidget(m_varsContainer);

    // ── Root layout ───────────────────────────────────────────────────────────
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(mainArea, 1);
    root->addWidget(sidebar);

    // ── Category nav panel (floating, top-right) ─────────────────────────────
    auto* navPanel = new CategoryNavPanel(this);
    m_categoryNav = navPanel;
    navPanel->onCategoryClicked = [this](const QString& displayName) {
        auto it = m_groupHeaders.find(displayName);
        if (it != m_groupHeaders.end() && it.value()) {
            const int y = it.value()->mapTo(m_groupsScroll->widget(), QPoint(0, 0)).y();
            m_groupsScroll->verticalScrollBar()->setValue(y);
        }
    };

    // ── Clear button (floating, just left of nav panel) ───────────────────────
    m_clearBtn = new QPushButton("✕  Clear", this);
    m_clearBtn->setObjectName("ComposerClearBtn");
    m_clearBtn->setFixedHeight(26);
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    connect(m_clearBtn, &QPushButton::clicked, this, [this]() {
        // Deactivate all rules
        for (Rule& rule : m_rules->rules())
            rule.enabled = false;
        m_rules->saveToFile(BASE_PATH + "/" + RULES_PATH);
        m_suppressRuleSave = true;
        rebuildRulesSidebar();
        m_suppressRuleSave = false;

        // Clear all active tags and entry pushes
        m_activeTags.clear();
        m_activeTagSet.clear();
        m_deactivatedTags.clear();
        m_tagWeights.clear();
        m_activePushes.clear();

        // Notify tile view that no entries are toggled
        emit activeGroupsChanged({});

        // Deactivate all LoRAs
        m_activeLoraUuids.clear();
        emit loraUuidsRestored({});

        repush();
    });

    // ── Preview image label (floating, bottom-right) ──────────────────────────
    static constexpr int PreviewSize = 200;
    m_previewLabel = new PreviewClickLabel(this);
    m_previewLabel->setObjectName("ComposerPreviewLabel");
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setFixedSize(PreviewSize, PreviewSize);
    m_previewLabel->hide(); // shown when first image arrives
    // Step text is painted directly inside PreviewClickLabel::paintEvent

    // ── Control bar (floating below preview) ──────────────────────────────────
    m_controlBar = new QWidget(this);
    m_controlBar->setObjectName("ComposerControlBar");
    m_controlBar->setAttribute(Qt::WA_StyledBackground, true);
    m_controlBar->setFixedHeight(36);

    m_copyBtn = new QPushButton("Copy prompt", m_controlBar);
    m_copyBtn->setObjectName("ComposerCopyBtn");
    m_copyBtn->setCursor(Qt::PointingHandCursor);
    connect(m_copyBtn, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(currentPromptString(false));
    });

    m_runBtn = new QPushButton("▶ Run", m_controlBar);
    m_runBtn->setObjectName("ComposerRunBtn");
    m_runBtn->setCursor(Qt::PointingHandCursor);

    m_promptCountSpin = new QSpinBox(m_controlBar);
    m_promptCountSpin->setObjectName("ComposerCountSpin");
    m_promptCountSpin->setRange(1, 99);
    m_promptCountSpin->setValue(1);
    m_promptCountSpin->setFixedWidth(46);

    m_interruptBtn = new QPushButton("×", m_controlBar);
    m_interruptBtn->setObjectName("ComposerInterruptBtn");
    m_interruptBtn->setFixedSize(25, 25);
    m_interruptBtn->setCursor(Qt::PointingHandCursor);
    m_interruptBtn->setToolTip("Interrupt");

    m_queueLabel = new QLabel("0 active", m_controlBar);
    m_queueLabel->setObjectName("ComposerQueueLabel");

    auto* barLayout = new QHBoxLayout(m_controlBar);
    barLayout->setContentsMargins(5, 4, 8, 4);
    barLayout->setSpacing(4);
    barLayout->addWidget(m_copyBtn);
    barLayout->addWidget(m_runBtn, 1);
    barLayout->addWidget(m_promptCountSpin);
    barLayout->addWidget(m_interruptBtn);
    barLayout->addSpacing(2);
    barLayout->addWidget(m_queueLabel);
    m_controlBar->adjustSize();

    connect(m_runBtn, &QPushButton::clicked, this, [this]() {
        emit runRequested(m_promptCountSpin->value());
    });
    connect(m_interruptBtn, &QPushButton::clicked, this, [this]() {
        emit interruptRequested();
    });

    // ── Open popout on preview click ──────────────────────────────────────────
    connect(m_previewLabel, &PreviewClickLabel::clicked, this, [this]() {
        if (!m_popout) {
            auto* popout = new PreviewPopoutWindow(nullptr); // null parent → real top-level (FancyZones)
            popout->setAttribute(Qt::WA_DeleteOnClose);
            m_popout = popout;
            m_popout->installEventFilter(this);
            connect(m_popout, &QObject::destroyed, this, [this]() { m_popout = nullptr; });
            if (!m_tempFolder.isEmpty())
                popout->setTempFolder(m_tempFolder);
        }
        if (!m_currentPix.isNull())
            static_cast<PreviewPopoutWindow*>(m_popout)->setImage(m_currentPix);
        m_popout->show();
        m_popout->raise();
        m_popout->activateWindow();
    });

    // ── Wire pipeline ─────────────────────────────────────────────────────────
    connect(m_pipeline, &PromptPipeline::pipelineReady,
            this, &PromptComposerPage::onPipelineReady);

    rebuildRulesSidebar();
}

// ── showEvent / eventFilter ───────────────────────────────────────────────────

void PromptComposerPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    repositionFloats();
}

bool PromptComposerPage::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_statesList && event->type() == QEvent::Leave) {
        hideStatePreview();
        return false;
    }
    if (obj == m_popout) {
        if (event->type() == QEvent::Show) {
            // Popout opened — hide the floating preview label to reduce clutter
            m_previewLabel->hide();
        } else if (event->type() == QEvent::Hide) {
            // Popout closed — restore preview label if we have an image
            if (!m_currentPix.isNull()) {
                m_previewLabel->show();
                repositionFloats();
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

// ── Preview & control bar ─────────────────────────────────────────────────────

void PromptComposerPage::setPreviewImage(const QImage& image)
{
    if (image.isNull()) return;
    m_currentPix = QPixmap::fromImage(image);
    // Use the known fixed size directly — size() can return 0×0 on first call
    // when the floating label hasn't been laid out yet.
    m_previewLabel->setPixmap(
        m_currentPix.scaled(
            QSize(200, 200),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation));

    const bool popoutOpen = m_popout && m_popout->isVisible();
    if (!popoutOpen) {
        m_previewLabel->show();
        m_previewLabel->raise();
    }
    repositionFloats();

    if (popoutOpen)
        static_cast<PreviewPopoutWindow*>(m_popout)->setImage(m_currentPix);
}

void PromptComposerPage::setQueueCount(int count)
{
    m_queueLabel->setText(QString("%1 active").arg(count));
    repositionFloats();
}

void PromptComposerPage::setPreviewProgress(int step, int total)
{
    if (total > 0 && step > 0)
        m_previewLabel->setStepText(QString("%1 / %2").arg(step).arg(total));
}

void PromptComposerPage::triggerRun()
{
    emit runRequested(m_promptCountSpin->value());
}

void PromptComposerPage::setOutputFolderPattern(const QString& pattern)
{
    m_outputFolderPattern = pattern;
}

void PromptComposerPage::setTempFolder(const QString& folder)
{
    m_tempFolder = folder;
    if (m_popout)
        static_cast<PreviewPopoutWindow*>(m_popout)->setTempFolder(folder);
}

void PromptComposerPage::repositionFloats()
{
    constexpr int sidebarW = 220;
    constexpr int marginR  = 160;
    constexpr int marginT  = 42;
    constexpr int marginB  = 12;
    constexpr int gap      = 4;
    const int right  = width() - sidebarW - marginR;
    int       bottom = height() - marginB;

    if (m_categoryNav) {
        m_categoryNav->move(right - m_categoryNav->width(), marginT);
        m_categoryNav->raise();
    }
    if (m_clearBtn) {
        constexpr int btnGap = 2;
        const int navLeft = m_categoryNav ? m_categoryNav->x() : right;
        m_clearBtn->move(navLeft - m_clearBtn->width() - btnGap, marginT);
        m_clearBtn->raise();
    }

    m_controlBar->move(right - m_controlBar->width(),
                       bottom - m_controlBar->height());
    bottom -= m_controlBar->height() + gap;

    if (m_previewLabel->isVisible()) {
        m_previewLabel->move(right - m_previewLabel->width(),
                             bottom - m_previewLabel->height());
    }
    // step text is painted inside m_previewLabel via paintEvent — no separate widget
}

void PromptComposerPage::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    repositionFloats();
}

// ── Public API ────────────────────────────────────────────────────────────────

QString PromptComposerPage::currentPromptString(bool forJson) const
{
    // Re-bucket m_lastResult in groups.fct display order so the prompt
    // matches the visual ordering the user sees on screen.
    QHash<QString, QList<PipelineTag>> buckets;
    QList<QString> order;
    for (const TagGroup& g : m_groups.groups())
        order << g.name;
    order << ""; // uncategorized

    for (const PipelineTag& pt : m_lastResult) {
        if (pt.result == RuleResult::Deactivated) continue;
        buckets[m_groups.groupFor(pt.facets)] << pt;
    }

    QList<CategoryGroup> ordered;
    for (const QString& name : order) {
        if (!buckets.contains(name) || buckets[name].isEmpty()) continue;
        CategoryGroup cg;
        cg.category = name;
        cg.tags     = buckets[name];
        ordered << cg;
    }

    return PromptPipeline::buildPromptString(ordered, forJson);
}

void PromptComposerPage::setDanbooruIndex(core::DanbooruIndex* index)
{
    m_searchBar->setIndex(index);
}

void PromptComposerPage::setVariableIndex(core::VariableIndex* index)
{
    m_varIndex = index;
    rebuildVarsSidebar();
}

void PromptComposerPage::setWorkflowManager(core::WorkflowManager* wm, const QString& savePath)
{
    m_wfManager  = wm;
    m_wfSavePath = savePath;
    rebuildWorkflowList();
}

void PromptComposerPage::loadPipeline(int entryId, int imageIdx, const QList<QString>& tags)
{
    const qint64 key = (qint64(entryId) << 32) | quint32(imageIdx);
    const bool wasActive = m_activePushes.contains(key);

    if (wasActive) {
        QSet<QString> otherTags;
        for (auto it = m_activePushes.cbegin(); it != m_activePushes.cend(); ++it) {
            if (it.key() != key) {
                for (const QString& t : it.value()) otherTags.insert(t);
            }
        }
        for (const QString& tag : m_activePushes[key]) {
            if (!otherTags.contains(tag)) {
                m_activeTags.removeOne(tag);
                m_activeTagSet.remove(tag);
                m_tagWeights.remove(tag);
                m_deactivatedTags.remove(tag);
            }
        }
        m_activePushes.remove(key);
    } else {
        for (const QString& tag : tags) {
            if (!m_activeTagSet.contains(tag)) {
                m_activeTags << tag;
                m_activeTagSet.insert(tag);
            }
        }
        m_activePushes[key] = tags;
    }

    // ── LoRA: sync activation with push state ─────────────────────────────
    core::Entry* entry = m_entryModel ? m_entryModel->entryById(entryId) : nullptr;
    if (entry && entry->lora.has_value()) {
        const QString uuid = entry->uuid;
        bool loraChanged = false;
        if (!wasActive) {
            if (!m_activeLoraUuids.contains(uuid)) {
                m_activeLoraUuids.append(uuid);
                loraChanged = true;
            }
        } else {
            bool stillPushed = false;
            for (auto it = m_activePushes.cbegin(); it != m_activePushes.cend(); ++it) {
                if (int(quint32(it.key() >> 32)) == entryId) { stillPushed = true; break; }
            }
            if (!stillPushed && m_activeLoraUuids.removeAll(uuid) > 0)
                loraChanged = true;
        }
        if (loraChanged)
            emit loraUuidsRestored(m_activeLoraUuids);
    }

    QMap<int, QList<int>> activeGroups;
    for (auto it = m_activePushes.cbegin(); it != m_activePushes.cend(); ++it) {
        int eid = int(quint32(it.key() >> 32));
        int img = int(quint32(it.key() & 0xFFFFFFFFLL));
        activeGroups[eid].append(img);
    }
    emit activeGroupsChanged(activeGroups);
    repush();
}

// ── Entry tag sync ────────────────────────────────────────────────────────────

void PromptComposerPage::onEntryTagAdded(int entryId, int imageIdx, const QString& tag)
{
    const qint64 key = (qint64(entryId) << 32) | quint32(imageIdx);
    if (!m_activePushes.contains(key)) return;

    if (!m_activePushes[key].contains(tag))
        m_activePushes[key] << tag;

    if (!m_activeTagSet.contains(tag)) {
        m_activeTags << tag;
        m_activeTagSet.insert(tag);
    }
    QMetaObject::invokeMethod(this, &PromptComposerPage::repush, Qt::QueuedConnection);
}

void PromptComposerPage::onEntryTagRemoved(int entryId, int imageIdx, const QString& tag)
{
    const qint64 key = (qint64(entryId) << 32) | quint32(imageIdx);
    if (!m_activePushes.contains(key)) return;

    m_activePushes[key].removeOne(tag);

    // Only drop from active tags if no other active push still references it
    bool stillNeeded = false;
    for (auto it = m_activePushes.cbegin(); it != m_activePushes.cend(); ++it)
        if (it.value().contains(tag)) { stillNeeded = true; break; }

    if (!stillNeeded) {
        m_activeTags.removeOne(tag);
        m_activeTagSet.remove(tag);
        m_tagWeights.remove(tag);
        m_deactivatedTags.remove(tag);
    }
    QMetaObject::invokeMethod(this, &PromptComposerPage::repush, Qt::QueuedConnection);
}

// ── Pipeline ──────────────────────────────────────────────────────────────────

void PromptComposerPage::repush()
{
    QList<QString> active;
    for (const QString& t : m_activeTags)
        if (!m_deactivatedTags.contains(t))
            active << t;
    m_pipeline->push(active);
}

void PromptComposerPage::onPipelineReady(QList<core::CategoryGroup> categoryGroups)
{
    // Apply user-set weights before storing or displaying
    for (auto& g : categoryGroups)
        for (auto& pt : g.tags)
            pt.weight = m_tagWeights.value(pt.tag, 1.0f);

    m_lastGroups = categoryGroups;

    QList<PipelineTag> flat;
    for (const auto& g : categoryGroups)
        flat << g.tags;

    // Append deactivated tags as display-only entries (not in pipeline output)
    for (const QString& tag : m_activeTags) {
        if (m_deactivatedTags.contains(tag)) {
            PipelineTag pt;
            pt.tag    = tag;
            pt.result = RuleResult::Deactivated;
            flat << pt;
        }
    }

    m_lastResult = flat;
    applyTagFilter();
}

// ── Groups display ────────────────────────────────────────────────────────────

void PromptComposerPage::applyTagFilter()
{
    if (m_filterQuery.isEmpty()) {
        rebuildGroupsDisplay(m_lastResult);
        return;
    }
    QList<PipelineTag> filtered;
    for (const PipelineTag& pt : m_lastResult) {
        const bool matchTag = pt.tag.startsWith(m_filterQuery, Qt::CaseInsensitive);
        const bool matchSrc = !pt.sourceTag.isEmpty()
                           && pt.sourceTag.startsWith(m_filterQuery, Qt::CaseInsensitive);
        if (matchTag || matchSrc)
            filtered << pt;
    }
    rebuildGroupsDisplay(filtered);
}

void PromptComposerPage::rebuildGroupsDisplay(const QList<PipelineTag>& flat)
{
    const bool freeze = m_freezeNextRebuild;
    m_freezeNextRebuild = false;
    if (freeze) setUpdatesEnabled(false);

    // Freeze will redraw and flicker but should be fine for large redraws
    if (freeze) {
        while (m_groupsLayout->count() > 0)
        {
            QLayoutItem* item = m_groupsLayout->takeAt(0);
            delete item->widget();
            delete item;
        }
    }
    else { // Do not flicker for small incremental layout changes
        while (m_groupsLayout->count() > 0)
        {
            QLayoutItem* item = m_groupsLayout->takeAt(0);
            if (QWidget* w = item->widget()) w->deleteLater();
            delete item;
        }
    }

    m_groupHeaders.clear();

    if (flat.isEmpty()) {
        if (freeze) setUpdatesEnabled(true);
        m_mainStack->setCurrentIndex(0);
        if (m_categoryNav)
            static_cast<CategoryNavPanel*>(m_categoryNav)->updateCategories({});
        return;
    }
    m_mainStack->setCurrentIndex(1);

    QHash<QString, QList<PipelineTag>> buckets;
    QList<QString> order;
    for (const TagGroup& g : m_groups.groups())
        order << g.name;
    order << "";

    QList<PipelineTag> deactivatedBucket;
    for (const PipelineTag& pt : flat) {
        if (pt.result == RuleResult::Deactivated)
            deactivatedBucket << pt;
        else
            buckets[m_groups.groupFor(pt.facets)] << pt;
    }

    QStringList navNames;
    for (const QString& grpName : order) {
        if (!buckets.contains(grpName)) continue;

        const QString displayName = grpName.isEmpty() ? "Uncategorized" : grpName;
        auto* header = new QLabel(displayName);
        header->setObjectName("ComposerGroupHeader");
        m_groupsLayout->addWidget(header);
        m_groupHeaders[displayName] = header;
        navNames << displayName;

        for (const PipelineTag& pt : buckets[grpName])
            m_groupsLayout->addWidget(makeTagRow(pt));

        auto* spacer = new QWidget;
        spacer->setFixedHeight(6);
        m_groupsLayout->addWidget(spacer);
    }

    if (!deactivatedBucket.isEmpty()) {
        const QString displayName = "Deactivated";
        auto* header = new QLabel(displayName);
        header->setObjectName("ComposerGroupHeader");
        m_groupsLayout->addWidget(header);
        m_groupHeaders[displayName] = header;
        navNames << displayName;
        for (const PipelineTag& pt : deactivatedBucket)
            m_groupsLayout->addWidget(makeTagRow(pt));
        auto* spacer = new QWidget;
        spacer->setFixedHeight(6);
        m_groupsLayout->addWidget(spacer);
    }

    m_groupsLayout->addStretch();

    if (freeze) setUpdatesEnabled(true);

    if (m_categoryNav)
        static_cast<CategoryNavPanel*>(m_categoryNav)->updateCategories(navNames);
}

// ── Tag row ───────────────────────────────────────────────────────────────────

QWidget* PromptComposerPage::makeTagRow(const PipelineTag& pt)
{
    const bool hasVar        = !pt.sourceTag.isEmpty();
    const bool isDeactivated = (pt.result == RuleResult::Deactivated);
    const bool inActive      = (pt.result != RuleResult::Injected);
    const bool editable      = !hasVar && !isDeactivated
                            && (pt.result == RuleResult::Include
                             || pt.result == RuleResult::NoFacets
                             || pt.result == RuleResult::Flagged);

    const QString activeKey = hasVar ? pt.sourceTag : pt.tag;

    auto* row = new QWidget;
    row->setObjectName("ComposerTagRow");

    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(20, 1, 8, 1);
    rl->setSpacing(8);

    auto* dot = new QWidget;
    dot->setFixedSize(6, 6);
    dot->setStyleSheet(
        QString("background:%1;border-radius:3px;").arg(dotColorFor(pt.result)));
    rl->addWidget(dot, 0, Qt::AlignVCenter);

    auto* tagEdit = new QLineEdit(pt.tag);
    tagEdit->setObjectName(editable ? "ComposerTagEdit" : "ComposerTagReadOnly");
    tagEdit->setReadOnly(!editable);
    tagEdit->setProperty("_tag", activeKey);

    if (pt.result == RuleResult::Skipped || pt.result == RuleResult::Replaced) {
        QFont f = tagEdit->font();
        f.setStrikeOut(true);
        tagEdit->setFont(f);
    }

    if (editable) {
        connect(tagEdit, &QLineEdit::editingFinished, this, [this, tagEdit]() {
            const QString oldTag = tagEdit->property("_tag").toString();
            const QString newTag = tagEdit->text().trimmed();
            if (newTag.isEmpty() || newTag == oldTag) return;
            const int i = m_activeTags.indexOf(oldTag);
            if (i >= 0) {
                m_activeTags[i] = newTag;
                m_activeTagSet.remove(oldTag);
                m_activeTagSet.insert(newTag);
                tagEdit->setProperty("_tag", newTag);
                QMetaObject::invokeMethod(
                    this, &PromptComposerPage::repush, Qt::QueuedConnection);
            }
        });
    }

    rl->addWidget(tagEdit, 1);

    if (hasVar) {
        static const QRegularExpression varRe(R"(\$([A-Za-z0-9_]+)\$)");
        QStringList varNames;
        auto it = varRe.globalMatch(pt.sourceTag);
        while (it.hasNext()) varNames << "$" + it.next().captured(1) + "$";
        auto* badge = new QLabel(varNames.join(" "));
        badge->setObjectName("ComposerVarBadge");
        badge->setAttribute(Qt::WA_StyledBackground, true);
        rl->addWidget(badge);
    }
    if (pt.result == RuleResult::NoFacets) {
        auto* badge = new QLabel("?");
        badge->setObjectName("ComposerNoBadge");
        badge->setAttribute(Qt::WA_StyledBackground, true);
        rl->addWidget(badge);
    }
    if (!pt.flagLabel.isEmpty()) {
        auto* badge = new QLabel("[" + pt.flagLabel + "]");
        badge->setObjectName("ComposerFlagBadge");
        badge->setAttribute(Qt::WA_StyledBackground, true);
        rl->addWidget(badge);
    }
    if (pt.result == RuleResult::Injected) {
        auto* badge = new QLabel("↑ " + pt.ruleSource);
        badge->setObjectName("ComposerInjectedBadge");
        badge->setAttribute(Qt::WA_StyledBackground, true);
        rl->addWidget(badge);
    }

    // Weight spinbox — shown for tags that appear in the output (not deactivated/removed).
    if (pt.result != RuleResult::Skipped && pt.result != RuleResult::Replaced
        && pt.result != RuleResult::Deactivated) {
        auto* wSpin = new QDoubleSpinBox;
        wSpin->setObjectName("ComposerWeightSpin");
        wSpin->setRange(0.10, 5.00);
        wSpin->setSingleStep(0.05);
        wSpin->setDecimals(2);
        wSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        wSpin->setAlignment(Qt::AlignCenter);
        wSpin->setFixedWidth(52);
        wSpin->setValue(double(pt.weight));

        const QString weightKey = pt.tag;

        auto applyWeightColor = [wSpin](double val) {
            const bool weighted = qAbs(val - 1.0) > 0.001;
            wSpin->setProperty("weighted", weighted);
            wSpin->style()->unpolish(wSpin);
            wSpin->style()->polish(wSpin);
            wSpin->update();
        };
        applyWeightColor(double(pt.weight));

        connect(wSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this, weightKey, applyWeightColor](double val) {
                    const float w = float(val);
                    m_tagWeights[weightKey] = w;
                    for (auto& g : m_lastGroups)
                        for (auto& p : g.tags)
                            if (p.tag == weightKey) p.weight = w;
                    for (auto& p : m_lastResult)
                        if (p.tag == weightKey) p.weight = w;
                    applyWeightColor(val);
                });
        rl->addWidget(wSpin);
    }

    const QString wikiTag = pt.tag;
    if (inActive) {
        auto onRemove = [this, activeKey, wikiTag]() {
            m_activeTags.removeOne(activeKey);
            m_activeTagSet.remove(activeKey);
            m_tagWeights.remove(wikiTag);
            m_deactivatedTags.remove(activeKey);
            QMetaObject::invokeMethod(
                this, &PromptComposerPage::repush, Qt::QueuedConnection);
        };

        auto onToggleDeactivate = [this, activeKey]() {
            if (m_deactivatedTags.contains(activeKey))
                m_deactivatedTags.remove(activeKey);
            else
                m_deactivatedTags.insert(activeKey);
            QMetaObject::invokeMethod(
                this, &PromptComposerPage::repush, Qt::QueuedConnection);
        };

        auto* delBtn = new QPushButton("×", row);
        delBtn->setObjectName("TagRemoveBtn");
        delBtn->setFixedSize(18, 18);
        delBtn->setCursor(Qt::PointingHandCursor);
        connect(delBtn, &QPushButton::clicked, this, onRemove);
        rl->addWidget(delBtn);

        auto installMenu = [&](QWidget* w) {
            w->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(w, &QWidget::customContextMenuRequested, this,
                [this, wikiTag, onRemove, onToggleDeactivate, isDeactivated](const QPoint&) {
                    QMenu menu;
                    QAction* wikiAct   = menu.addAction("Wiki");
                    QAction* facetAct  = menu.addAction("Edit facets");
                    QAction* deactAct  = menu.addAction(isDeactivated ? "Activate" : "Deactivate");
                    QAction* removeAct = menu.addAction("Remove");
                    QAction* chosen = menu.exec(QCursor::pos());
                    if      (chosen == wikiAct)   emit wikiRequested(wikiTag);
                    else if (chosen == facetAct)  emit facetEditorRequested(wikiTag);
                    else if (chosen == deactAct)  onToggleDeactivate();
                    else if (chosen == removeAct) onRemove();
                });
        };
        installMenu(row);
        installMenu(tagEdit);
    } else {
        auto installWiki = [&](QWidget* w) {
            w->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(w, &QWidget::customContextMenuRequested, this,
                [this, wikiTag](const QPoint&) {
                    QMenu menu;
                    QAction* wikiAct  = menu.addAction("Wiki");
                    QAction* facetAct = menu.addAction("Edit facets");
                    QAction* chosen   = menu.exec(QCursor::pos());
                    if (chosen == wikiAct)       emit wikiRequested(wikiTag);
                    else if (chosen == facetAct) emit facetEditorRequested(wikiTag);
                });
        };
        installWiki(row);
        installWiki(tagEdit);
    }

    return row;
}

// ── Rules reload ─────────────────────────────────────────────────────────────

void PromptComposerPage::reloadRules()
{
    const QString path = BASE_PATH + "/" + RULES_PATH;
    QStringList errors;
    RuleEngine fresh = RuleEngine::loadFromFile(path, &errors);

    if (!errors.isEmpty()) {
        emit statusMessageRequested(errors.first());
        return;
    }

    m_rules->rules() = fresh.rules();
    emit statusMessageRequested(
        QString("Rules reloaded - %1 rule(s).").arg(m_rules->rules().size()));
    rebuildRulesSidebar();
    QMetaObject::invokeMethod(this, &PromptComposerPage::repush, Qt::QueuedConnection);
}

void PromptComposerPage::reloadVars()
{
    if (!m_varIndex) return;
    const QString path = BASE_PATH + "/" + VARS_PATH;
    VariableIndex fresh = VariableIndex::loadFromFile(path);
    m_varIndex->variables() = fresh.variables();
    emit statusMessageRequested(
        QString("Variables reloaded - %1 variable(s).").arg(m_varIndex->variables().size()));
    rebuildVarsSidebar();
    QMetaObject::invokeMethod(this, &PromptComposerPage::repush, Qt::QueuedConnection);
}

// ── Rules sidebar ─────────────────────────────────────────────────────────────

void PromptComposerPage::rebuildRulesSidebar()
{
    while (m_rulesLayout->count() > 0) {
        QLayoutItem* item = m_rulesLayout->takeAt(0);
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }

    QList<Rule>& rules = m_rules->rules();

    if (rules.isEmpty()) {
        auto* hint = new QLabel("No rules defined.\nEdit data/rules.fct to add some.");
        hint->setObjectName("ComposerRulesHint");
        hint->setWordWrap(true);
        hint->setAlignment(Qt::AlignTop);
        m_rulesLayout->addWidget(hint);
    } else {
        for (int i = 0; i < rules.size(); ++i) {
            const bool hasArgEdit = (rules[i].action.type == ActionType::Add
                                  || rules[i].action.type == ActionType::Replace);

            auto* ruleWidget = new QWidget;
            auto* rwl = new QVBoxLayout(ruleWidget);
            rwl->setContentsMargins(0, 0, 0, 2);
            rwl->setSpacing(2);

            auto* cbRow  = new QWidget;
            auto* cbRowL = new QHBoxLayout(cbRow);
            cbRowL->setContentsMargins(0, 0, 0, 0);
            cbRowL->setSpacing(4);

            auto* cb = new QCheckBox(rules[i].name);
            cb->setObjectName("ComposerRuleToggle");
            cb->setChecked(rules[i].enabled);
            connect(cb, &QCheckBox::toggled, this, [this, i](bool on) {
                m_rules->rules()[i].enabled = on;
                if (!m_suppressRuleSave)
                    m_rules->saveToFile(BASE_PATH + "/" + RULES_PATH);
                QMetaObject::invokeMethod(
                    this, &PromptComposerPage::repush, Qt::QueuedConnection);
            });
            cbRowL->addWidget(cb, 1);

            if (rules[i].force) {
                auto* forceBadge = new QLabel("F");
                forceBadge->setObjectName("ComposerRuleForceBadge");
                cbRowL->addWidget(forceBadge);
            }

            rwl->addWidget(cbRow);

            if (hasArgEdit) {
                auto* argEdit = new QLineEdit(rules[i].action.arguments.join(", "));
                argEdit->setObjectName("ComposerRuleArgEdit");
                argEdit->setPlaceholderText("tag to inject…");
                connect(argEdit, &QLineEdit::editingFinished, this, [this, i, argEdit]() {
                    QList<QString> args;
                    for (const QString& a : argEdit->text().split(','))
                        if (const QString t = a.trimmed(); !t.isEmpty())
                            args << t;
                    m_rules->rules()[i].action.arguments = args;
                    m_rules->saveToFile(BASE_PATH + "/" + RULES_PATH);
                    QMetaObject::invokeMethod(
                        this, &PromptComposerPage::repush, Qt::QueuedConnection);
                });

                const bool isReplace = (rules[i].action.type == ActionType::Replace);
                auto* actionBadge = new QLabel(isReplace ? "→" : "+");
                actionBadge->setObjectName(isReplace ? "ComposerRuleReplaceBadge"
                                                     : "ComposerRuleAddBadge");

                auto* argRow = new QWidget;
                auto* arl = new QHBoxLayout(argRow);
                arl->setContentsMargins(18, 0, 0, 0);
                arl->setSpacing(4);
                arl->addWidget(actionBadge);
                arl->addWidget(argEdit, 1);
                rwl->addWidget(argRow);
            }

            m_rulesLayout->addWidget(ruleWidget);
        }
    }

    m_rulesLayout->addStretch();
}

// ── Workflow sidebar ──────────────────────────────────────────────────────────

void PromptComposerPage::rebuildWorkflowList()
{
    if (!m_wfList) return;
    m_wfList->clear();

    if (!m_wfManager || m_wfManager->files().isEmpty()) {
        auto* hint = new QListWidgetItem("Drop .json files here");
        hint->setFlags(Qt::NoItemFlags);
        QFont f = hint->font();
        f.setItalic(true);
        hint->setFont(f);
        hint->setForeground(QColor("#2a2a2a"));
        m_wfList->addItem(hint);
        return;
    }

    for (int i = 0; i < m_wfManager->files().size(); ++i) {
        const bool sel = (i == m_wfManager->selectedIndex());
        auto* item = new QListWidgetItem(m_wfManager->files()[i].name);
        item->setData(Qt::UserRole, i);
        item->setToolTip(m_wfManager->files()[i].path);
        if (sel) {
            item->setForeground(QColor("#5a9a5a"));
        }
        m_wfList->addItem(item);
    }
}

// ── Variable sidebar ──────────────────────────────────────────────────────────

void PromptComposerPage::rebuildVarsSidebar()
{
    while (m_varsLayout->count() > 0) {
        QLayoutItem* item = m_varsLayout->takeAt(0);
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }

    if (!m_varIndex || m_varIndex->variables().isEmpty()) {
        auto* hint = new QLabel("No variables defined.\nEdit data/vars.fct to add some.");
        hint->setObjectName("ComposerRulesHint");
        hint->setWordWrap(true);
        m_varsLayout->addWidget(hint);
        return;
    }

    QList<Variable>& vars = m_varIndex->variables();
    for (int i = 0; i < vars.size(); ++i) {
        auto* row = new QWidget;
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(6);

        auto* nameLabel = new QLabel("$" + vars[i].name + "$");
        nameLabel->setObjectName("ComposerVarName");
        nameLabel->setFixedWidth(70);

        auto* edit = new QLineEdit(vars[i].value);
        edit->setObjectName("ComposerVarEdit");
        edit->setPlaceholderText("(empty)");

        connect(edit, &QLineEdit::editingFinished, this, [this, i, edit]() {
            m_varIndex->variables()[i].value = edit->text().trimmed();
            m_varIndex->saveToFile(BASE_PATH + "/" + VARS_PATH);
            QMetaObject::invokeMethod(
                this, &PromptComposerPage::repush, Qt::QueuedConnection);
        });

        rl->addWidget(nameLabel);
        rl->addWidget(edit, 1);
        m_varsLayout->addWidget(row);
    }
}

// ── Session save / restore ────────────────────────────────────────────────────

void PromptComposerPage::saveSession(const QString& path) const
{
    QJsonArray tagsArr;
    for (const QString& t : m_activeTags)
        tagsArr.append(t);

    QJsonObject weightsObj;
    for (auto it = m_tagWeights.constBegin(); it != m_tagWeights.constEnd(); ++it)
        if (qAbs(it.value() - 1.0f) >= 0.001f)
            weightsObj[it.key()] = double(it.value());

    QJsonArray pushesArr;
    for (auto it = m_activePushes.constBegin(); it != m_activePushes.constEnd(); ++it) {
        const int runtimeId = int(quint32(it.key() >> 32));
        const int imageIdx  = int(quint32(it.key() & 0xFFFFFFFFLL));
        core::Entry* entry  = m_entryModel ? m_entryModel->entryById(runtimeId) : nullptr;
        if (!entry || imageIdx >= entry->images.size()) continue;
        QJsonArray tagArr;
        for (const QString& t : it.value()) tagArr.append(t);
        QJsonObject o;
        o["uuid"]          = entry->uuid;
        o["imageFileName"] = entry->images[imageIdx].fileName;
        o["tags"]          = tagArr;
        pushesArr.append(o);
    }

    QJsonArray deactivatedArr;
    for (const QString& t : m_deactivatedTags)
        deactivatedArr.append(t);

    QJsonArray loraUuidsArr;
    for (const QString& uuid : m_activeLoraUuids) loraUuidsArr.append(uuid);

    QJsonObject root;
    root["activeTags"]      = tagsArr;
    root["tagWeights"]      = weightsObj;
    root["activePushes"]    = pushesArr;
    root["deactivatedTags"] = deactivatedArr;
    root["activeLoraUuids"] = loraUuidsArr;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(root).toJson());
}

void PromptComposerPage::restoreSession(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();

    m_activeTags.clear();
    m_activeTagSet.clear();
    m_tagWeights.clear();
    m_activePushes.clear();
    m_deactivatedTags.clear();

    for (const QJsonValue& v : root["activeTags"].toArray()) {
        const QString t = v.toString();
        if (!t.isEmpty() && !m_activeTagSet.contains(t)) {
            m_activeTags << t;
            m_activeTagSet.insert(t);
        }
    }

    const QJsonObject weightsObj = root["tagWeights"].toObject();
    for (auto it = weightsObj.constBegin(); it != weightsObj.constEnd(); ++it)
        m_tagWeights[it.key()] = float(it.value().toDouble(1.0));

    for (const QJsonValue& v : root["deactivatedTags"].toArray())
        if (const QString t = v.toString(); !t.isEmpty())
            m_deactivatedTags.insert(t);

    for (const QJsonValue& v : root["activePushes"].toArray()) {
        const QJsonObject o        = v.toObject();
        const QString uuid         = o["uuid"].toString();
        const QString imageFileName = o["imageFileName"].toString();
        if (uuid.isEmpty() || imageFileName.isEmpty()) continue;
        core::Entry* entry = m_entryModel ? m_entryModel->entryByUuid(uuid) : nullptr;
        if (!entry) continue;
        int imageIdx = -1;
        for (int i = 0; i < entry->images.size(); ++i)
            if (entry->images[i].fileName == imageFileName) { imageIdx = i; break; }
        if (imageIdx < 0) continue;
        QList<QString> tags;
        for (const QJsonValue& t : o["tags"].toArray())
            tags << t.toString();
        m_activePushes[(qint64(entry->id) << 32) | quint32(imageIdx)] = tags;
    }

    QMap<int, QList<int>> activeGroups;
    for (auto it = m_activePushes.constBegin(); it != m_activePushes.constEnd(); ++it) {
        const int eid = int(quint32(it.key() >> 32));
        const int img = int(quint32(it.key() & 0xFFFFFFFFLL));
        activeGroups[eid].append(img);
    }
    emit activeGroupsChanged(activeGroups);

    m_activeLoraUuids.clear();
    for (const QJsonValue& v : root["activeLoraUuids"].toArray())
        m_activeLoraUuids << v.toString();
    emit loraUuidsRestored(m_activeLoraUuids);

    if (!m_activeTags.isEmpty())
        repush();
}

// ── States ────────────────────────────────────────────────────────────────────

void PromptComposerPage::setStatesDir(const QString& dir)
{
    m_statesDir = dir;
    QDir().mkpath(dir);
    m_stateManager = core::StateManager::loadFromDir(dir);
    rebuildStatesList();
}

void PromptComposerPage::rebuildStatesList()
{
    if (!m_statesList) return;
    m_statesList->clear();

    const auto& states = m_stateManager.states();
    if (states.isEmpty()) {
        auto* item = new QListWidgetItem("No saved states");
        item->setFlags(Qt::NoItemFlags);
        QFont f = item->font(); f.setItalic(true);
        item->setFont(f);
        item->setForeground(QColor("#2a2a2a"));
        m_statesList->addItem(item);
        return;
    }

    for (const auto& state : states) {
        const bool hasImg = !state.previewImagePath.isEmpty()
                         && QFile::exists(state.previewImagePath);
        auto* item = new QListWidgetItem((hasImg ? "◆  " : "") + state.name);
        m_statesList->addItem(item);
    }
}

void PromptComposerPage::setEntryModel(model::EntryModel* model)
{
    m_entryModel = model;
}

void PromptComposerPage::setActiveLoraUuids(const QList<QString>& uuids)
{
    m_activeLoraUuids = uuids;
}

void PromptComposerPage::saveCurrentState()
{
    if (m_statesDir.isEmpty()) return;

    bool ok;
    const QString defaultName =
        QString("State %1").arg(m_stateManager.states().size() + 1);
    const QString name = QInputDialog::getText(
        this, "Save State", "Name:", QLineEdit::Normal, defaultName, &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    core::SavedState state;
    state.id              = QString::number(QDateTime::currentMSecsSinceEpoch());
    state.name            = name.trimmed();
    state.activeTags      = m_activeTags;
    for (auto it = m_tagWeights.cbegin(); it != m_tagWeights.cend(); ++it)
        if (qAbs(it.value() - 1.0f) >= 0.001f)
            state.tagWeights[it.key()] = it.value();
    state.deactivatedTags = m_deactivatedTags;

    // Convert runtime (entryId, imageIdx) keys to stable (uuid, imageFileName)
    for (auto it = m_activePushes.cbegin(); it != m_activePushes.cend(); ++it) {
        const int runtimeId = int(quint32(it.key() >> 32));
        const int imageIdx  = int(quint32(it.key() & 0xFFFFFFFFLL));
        core::Entry* entry  = m_entryModel ? m_entryModel->entryById(runtimeId) : nullptr;
        if (!entry || imageIdx >= entry->images.size()) continue;
        core::EntryPush ep;
        ep.uuid          = entry->uuid;
        ep.imageFileName = entry->images[imageIdx].fileName;
        ep.tags          = it.value();
        state.activePushes << ep;
    }

    for (const auto& rule : m_rules->rules())
        state.ruleStates[rule.name] = rule.enabled;

    if (m_varIndex)
        for (const auto& var : m_varIndex->variables())
            state.varValues[var.name] = var.value;

    if (m_wfManager) {
        const core::WorkflowFile* wf = m_wfManager->selectedFile();
        state.selectedWorkflowId = wf ? wf->id : QString();

        QJsonObject varValues;
        for (const auto& var : m_wfManager->variables()) {
            if (var.placeholder.isEmpty()) continue;
            QJsonObject o;
            switch (var.type) {
            case core::WorkflowVarType::Seed:
                o["seedBehavior"] = int(var.seedBehavior);
                o["seedValue"]    = var.seedValue;   // integer, not double
                break;
            case core::WorkflowVarType::String:
            case core::WorkflowVarType::LatentSize:
                o["stringValue"] = var.stringValue;
                break;
            case core::WorkflowVarType::Integer:
                o["intValue"] = var.intValue;
                break;
            case core::WorkflowVarType::Float:
                o["floatValue"] = var.floatValue;
                break;
            case core::WorkflowVarType::DirSearch:
                o["selectedFile"] = var.selectedFile;
                break;
            }
            varValues[var.placeholder] = o;
        }
        state.workflowVarValues = varValues;
    }

    state.activeLoraUuids = m_activeLoraUuids;

    m_stateManager.states() << state;
    m_stateManager.saveToDir(m_statesDir);
    rebuildStatesList();
    emit statusMessageRequested(QString("Saved: %1").arg(state.name));
}

void PromptComposerPage::restoreState(const core::SavedState& state)
{
    m_activeTags.clear();
    m_activeTagSet.clear();
    m_tagWeights.clear();
    m_deactivatedTags.clear();
    m_activePushes.clear();

    m_activeTags      = state.activeTags;
    for (const auto& t : m_activeTags) m_activeTagSet.insert(t);
    m_tagWeights      = state.tagWeights;
    m_deactivatedTags = state.deactivatedTags;

    // Resolve uuid+imageFileName back to runtime keys; count entries that no longer exist
    int missing = 0;
    for (const auto& ep : state.activePushes) {
        core::Entry* entry = m_entryModel ? m_entryModel->entryByUuid(ep.uuid) : nullptr;
        if (!entry) { ++missing; continue; }
        int imageIdx = -1;
        for (int i = 0; i < entry->images.size(); ++i) {
            if (entry->images[i].fileName == ep.imageFileName) { imageIdx = i; break; }
        }
        if (imageIdx < 0) { ++missing; continue; }
        const qint64 key = (qint64(entry->id) << 32) | quint32(imageIdx);
        m_activePushes[key] = ep.tags;
    }

    QMap<int, QList<int>> activeGroups;
    for (auto it = m_activePushes.cbegin(); it != m_activePushes.cend(); ++it) {
        activeGroups[int(quint32(it.key() >> 32))].append(
            int(quint32(it.key() & 0xFFFFFFFFLL)));
    }
    emit activeGroupsChanged(activeGroups);

    m_activeLoraUuids = state.activeLoraUuids;
    emit loraUuidsRestored(m_activeLoraUuids);

    // Restore rule toggle states (in-memory only — don't touch rules.fct)
    if (!state.ruleStates.isEmpty()) {
        for (auto& rule : m_rules->rules()) {
            auto it = state.ruleStates.find(rule.name);
            if (it != state.ruleStates.end()) rule.enabled = it.value();
        }
        m_suppressRuleSave = true;
        rebuildRulesSidebar();
        m_suppressRuleSave = false;
    }

    // Restore variable values and persist to vars.fct
    if (m_varIndex && !state.varValues.isEmpty()) {
        for (auto& var : m_varIndex->variables()) {
            auto it = state.varValues.find(var.name);
            if (it != state.varValues.end()) var.value = it.value();
        }
        m_varIndex->saveToFile(BASE_PATH + "/" + VARS_PATH);
        rebuildVarsSidebar();
    }

    // Restore workflow selection first (so var restore targets the right workflow)
    bool workflowMissing = false;
    if (m_wfManager && !state.selectedWorkflowId.isEmpty()) {
        const int wfIdx = m_wfManager->workflowIndexById(state.selectedWorkflowId);
        if (wfIdx >= 0) {
            m_wfManager->setSelectedIndex(wfIdx);
            rebuildWorkflowList();
        } else {
            workflowMissing = true;
        }
    }

    // Restore workflow variable values into the now-selected workflow
    if (m_wfManager && !state.workflowVarValues.isEmpty()) {
        for (auto& var : m_wfManager->variables()) {
            if (!state.workflowVarValues.contains(var.placeholder)) continue;
            const QJsonObject o = state.workflowVarValues[var.placeholder].toObject();
            switch (var.type) {
            case core::WorkflowVarType::Seed:
                var.seedBehavior = core::SeedBehavior(o["seedBehavior"].toInt(int(var.seedBehavior)));
                var.seedValue    = o["seedValue"].toInteger(var.seedValue);
                break;
            case core::WorkflowVarType::String:
            case core::WorkflowVarType::LatentSize:
                var.stringValue = o["stringValue"].toString(var.stringValue);
                break;
            case core::WorkflowVarType::Integer:
                var.intValue = o["intValue"].toInt(var.intValue);
                break;
            case core::WorkflowVarType::Float:
                var.floatValue = o["floatValue"].toDouble(var.floatValue);
                break;
            case core::WorkflowVarType::DirSearch:
                var.selectedFile = o["selectedFile"].toString(var.selectedFile);
                break;
            }
        }
    }

    if (m_wfManager) {
        m_wfManager->saveToFile(m_wfSavePath);
        emit workflowVarsChanged();
    }

    repush();

    QStringList warnings;
    if (workflowMissing)
        warnings << "workflow no longer exists";
    if (missing > 0)
        warnings << QString("%1 entr%2 no longer exist").arg(missing).arg(missing == 1 ? "y" : "ies");

    if (warnings.isEmpty())
        emit statusMessageRequested(QString("Restored: %1").arg(state.name));
    else
        emit statusMessageRequested(
            QString("Restored: %1  (%2)").arg(state.name, warnings.join(", ")));
}

void PromptComposerPage::showStatePreview(int row)
{
    if (row < 0 || row >= m_stateManager.states().size()) {
        hideStatePreview(); return;
    }
    const core::SavedState& state = m_stateManager.states()[row];
    if (state.previewImagePath.isEmpty() || !QFile::exists(state.previewImagePath)) {
        hideStatePreview(); return;
    }
    QPixmap pix(state.previewImagePath);
    if (pix.isNull()) { hideStatePreview(); return; }

    pix = pix.scaled(220, 220, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_statesPreviewPopup->setPixmap(pix);
    m_statesPreviewPopup->adjustSize();

    const QRect itemRect = m_statesList->visualRect(
        m_statesList->model()->index(row, 0));
    const QPoint globalTopLeft =
        m_statesList->viewport()->mapToGlobal(itemRect.topLeft());
    int x = globalTopLeft.x() - m_statesPreviewPopup->width() - 8;
    int y = globalTopLeft.y();

    if (QScreen* scr = QGuiApplication::screenAt(globalTopLeft)) {
        const QRect sg = scr->availableGeometry();
        y = qBound(sg.top(), y, sg.bottom() - m_statesPreviewPopup->height());
    }

    m_statesPreviewPopup->move(x, y);
    m_statesPreviewPopup->show();
    m_statesPreviewPopup->raise();
}

void PromptComposerPage::hideStatePreview()
{
    if (m_statesPreviewPopup) m_statesPreviewPopup->hide();
}

ComposerScrollArea::ComposerScrollArea(QWidget* parent) : QScrollArea(parent) {}

void ComposerScrollArea::keyPressEvent(QKeyEvent* event)
{
    if (event->modifiers() & Qt::ShiftModifier)
    {
        if (event->key() == Qt::Key_E) {
            emit runRequested();
        }
        else if (event->key() == Qt::Key_R) {
            if (event->modifiers() & Qt::AltModifier) {
                emit clearPendingRequested();
            }
            else {
                emit interruptRequested();
            }
        }
    }
    event->ignore();
}

} // namespace gui
