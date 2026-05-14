#include <gui/composer/promptcomposerpage.h>
#include <gui/widgets/appscrollbar.h>
#include <gui/composer/categorynavpanel.h>
#include <gui/composer/composerscrollarea.h>
#include <gui/composer/previewclicklabel.h>
#include <gui/composer/previewpopoutwindow.h>
#include <gui/composer/stateslistwidget.h>
#include <gui/composer/workflowdroplist.h>
#include <gui/widgets/composericons.h>
#include <core/entrymodel.h>
#include <utils/appconfig.h>
#include <utils/qutils.h>
#include <QFile>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QBoxLayout>
#include <QStackedLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QMenu>
#include <QCursor>
#include <QApplication>
#include <QGuiApplication>
#include <QClipboard>
#include <QRegularExpression>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QInputDialog>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QDir>
#include <QDate>
#include <QDateTime>
#include <QFrame>
#include <QImage>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QScreen>
#include <QMouseEvent>
#include <QDoubleSpinBox>
#include <algorithm>
#include <QShortcut>

using namespace core;
using namespace utils;

namespace gui {

// `path/{yyyy-MM-dd}/...` -> resolved against current date; raw if no braces.
static QString resolveDatePattern(const QString& pattern)
{
    const int open = pattern.indexOf(QLatin1Char('{'));
    if (open < 0) return pattern;
    const int close = pattern.indexOf(QLatin1Char('}'), open);
    if (close < 0) return pattern.left(open);
    return pattern.left(open) +
           QDateTime::currentDateTime().toString(pattern.mid(open + 1, close - open - 1)) +
           pattern.mid(close + 1);
}


// ---- Helpers

static QString dotColorFor(RuleResult r)
{
    switch (r) {
    case RuleResult::Include:
        return "#336633";
    case RuleResult::Injected:
        return "#44bb44";
    case RuleResult::Skipped:
        return "#2a2a2a";
    case RuleResult::Replaced:
        return "#552222";
    case RuleResult::Flagged:
        return "#886622";
    case RuleResult::NoFacets:
        return "#334466";
    case RuleResult::Deactivated:
        return "#2a2a2a";
    case RuleResult::Deleted:
        return "#2a2a2a"; // unreachable; kept for switch exhaustiveness
    }
    return "#444444";
}

// Substitutes {yyyy-MM-dd} (or any Qt date spec) with today's date.
static QString resolveOutputPath(const QString& pattern)
{
    if (pattern.isEmpty()) return {};
    const QDate today = QDate::currentDate();
    QString result;
    result.reserve(pattern.size() + 20);
    for (int i = 0; i < pattern.size();) {
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

// Cap stored size: previews are shown at 220 px max (showStatePreview).
static constexpr int STATE_PREVIEW_MAX_W = 1024;
static constexpr int STATE_PREVIEW_MAX_H = 1280;

static bool saveStatePreview(const QString& src, const QString& dst)
{
    QImage img(src);
    if (img.isNull()) return QFile::copy(src, dst);
    if (img.width() > STATE_PREVIEW_MAX_W || img.height() > STATE_PREVIEW_MAX_H) {
        img = img.scaled(STATE_PREVIEW_MAX_W, STATE_PREVIEW_MAX_H, Qt::KeepAspectRatio,
                         Qt::SmoothTransformation);
    }
    return img.save(dst, "PNG");
}

// ---- Ctor

PromptComposerPage::PromptComposerPage(PromptPipeline* pipeline, RuleEngine* rules,
                                       const TagGroupIndex& groups, QWidget* parent)
    : QWidget(parent), m_pipeline(pipeline), m_rules(rules), m_groups(groups)
{
    setObjectName("PromptComposerPage");
    setAttribute(Qt::WA_StyledBackground, true);
    // Focusable so an Escape from the search bar can land here when the
    // groups scroll isn't visible (e.g. empty composer).
    setFocusPolicy(Qt::StrongFocus);

    // ---- Search bar
    m_searchBar = new TagSearchBar(this);
    m_searchBar->setActiveTags(&m_activeTagSet);
    m_filterDebounceTimer = new QTimer(this);
    m_filterDebounceTimer->setSingleShot(true);
    m_filterDebounceTimer->setInterval(180);
    connect(m_filterDebounceTimer, &QTimer::timeout, this, [this]() {
        m_freezeNextRebuild = true; // routes rebuildGroupsDisplay through the fade path
        applyTagFilter();
    });
    connect(m_searchBar, &TagSearchBar::queryChanged, this, [this](const QString& text) {
        m_filterQuery = text.trimmed();
        m_filterDebounceTimer->start();
    });
    connect(m_searchBar, &TagSearchBar::tagAdded, this, [this](const QString& tag) {
        // Split on commas so a paste / typed "a, b" lands as two distinct
        // tags - mirrors the entry panel's expectation. The search bar
        // commits one signal per tag in most cases, but defending here
        // covers any caller that hands us a comma-joined string.
        QStringList parts;
        if (tag.contains(','))
            parts = tag.split(',', Qt::SkipEmptyParts);
        else
            parts << tag;

        bool added = false;
        for (const QString& raw : parts) {
            const QString t = raw.trimmed();
            if (t.isEmpty() || m_activeTagSet.contains(t)) continue;
            m_activeTags << t;
            m_activeTagSet.insert(t);
            added = true;
        }
        if (!added) return;
        m_freezeNextRebuild = true;
        captureUndoSnapshot();
        queueRepush();
    });
    connect(m_searchBar, &TagSearchBar::downArrowOnEmpty, this, [this]() {
        if (m_tagRowWidgets.isEmpty() || !m_groupsScroll) return;
        m_groupsScroll->setFocus(Qt::OtherFocusReason);
        const int next = (m_selectedRowIdx < 0)
                             ? 0
                             : std::min(m_selectedRowIdx + 1,
                                        int(m_tagRowWidgets.size()) - 1);
        setSelectedRow(next);
    });
    connect(m_searchBar, &TagSearchBar::escapePressed, this, [this]() {
        // Prefer the groups scroll (so arrow keys / wheel scroll work);
        // fall back to the page itself when the empty-state hint is up so
        // Escape still moves focus out of the search bar in that case.
        QWidget* target = (m_groupsScroll && m_groupsScroll->isVisible())
                              ? static_cast<QWidget*>(m_groupsScroll)
                              : static_cast<QWidget*>(this);
        target->setFocus(Qt::OtherFocusReason);
    });

    // ---- Main groups area
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
    connect(groupsScroll, &ComposerScrollArea::runRequested, this,
            [this]() { emit runRequested(m_promptCountSpin->value()); });
    connect(groupsScroll, &ComposerScrollArea::interruptRequested, this,
            &PromptComposerPage::interruptRequested);
    connect(groupsScroll, &ComposerScrollArea::clearPendingRequested, this,
            &PromptComposerPage::clearPendingRequested);

    // Tab from inside the list jumps to the search bar, overriding
    // the default tag-row focus traversal.
    auto* tabToSearch = new QShortcut(QKeySequence(Qt::Key_Tab), groupsScroll);
    tabToSearch->setContext(Qt::WidgetWithChildrenShortcut);
    connect(tabToSearch, &QShortcut::activated, this,
            [this]() { m_searchBar->setFocus(Qt::TabFocusReason); });

    // Up/Down on the focused groups scroll selects neighboring tag rows.
    groupsScroll->installEventFilter(this);

    auto* emptyHint =
        new QLabel("Press \"Composer Toggle\" on an entry image\nto push its tags here.");
    emptyHint->setObjectName("ComposerEmptyHint");
    emptyHint->setAlignment(Qt::AlignCenter);

    m_mainStack = new QStackedWidget;
    m_mainStack->addWidget(emptyHint);    // 0
    m_mainStack->addWidget(groupsScroll); // 1

    // Fade around big rebuilds (manual add via search, state restore).
    // Replaces setUpdatesEnabled(false) which left widgets half-rendered.
    m_mainStackFx = new QGraphicsOpacityEffect(m_mainStack);
    m_mainStackFx->setOpacity(1.0);
    m_mainStack->setGraphicsEffect(m_mainStackFx);
    m_mainStackFade = new QPropertyAnimation(m_mainStackFx, "opacity", this);
    m_mainStackFade->setDuration(180);
    m_mainStackFade->setEasingCurve(QEasingCurve::InOutSine);

    // ---- Center stacked layout (bg layer + content layer)
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

    // ---- Rule sidebar
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

    auto* rulesOpenBtn = new QPushButton;
    rulesOpenBtn->setObjectName("SidebarBtn");
    rulesOpenBtn->setFixedSize(20, 20);
    rulesOpenBtn->setIcon(gui::icons::openExternal());
    rulesOpenBtn->setIconSize(QSize(14, 14));
    rulesOpenBtn->setCursor(Qt::PointingHandCursor);
    rulesOpenBtn->setToolTip("Open rules.fct in editor");
    connect(rulesOpenBtn, &QPushButton::clicked, this,
            []() { utils::openSystemFile(BASE_PATH + "/" + RULES_PATH); });

    auto* rulesReloadBtn = new QPushButton;
    rulesReloadBtn->setObjectName("SidebarBtn");
    rulesReloadBtn->setFixedSize(20, 20);
    rulesReloadBtn->setIcon(gui::icons::reload());
    rulesReloadBtn->setIconSize(QSize(14, 14));
    rulesReloadBtn->setCursor(Qt::PointingHandCursor);
    rulesReloadBtn->setToolTip("Reload rules from file");
    connect(rulesReloadBtn, &QPushButton::clicked, this, &PromptComposerPage::reloadRules);

    rhrL->addWidget(rulesHeaderLabel, 1);
    rhrL->addWidget(rulesOpenBtn);
    rhrL->addWidget(rulesReloadBtn);

    // ---- Variable editor section
    m_varsContainer = new QWidget;
    m_varsContainer->setObjectName("ComposerVarsContainer");
    m_varsLayout = new QVBoxLayout(m_varsContainer);
    m_varsLayout->setContentsMargins(8, 6, 8, 8);
    m_varsLayout->setSpacing(6);

    auto* varsSep = new QWidget;
    varsSep->setObjectName("ComposerHairlineSep");
    varsSep->setFixedHeight(1);

    auto* varsHeaderRow = new QWidget;
    varsHeaderRow->setObjectName("ComposerHeaderRow");
    varsHeaderRow->setAttribute(Qt::WA_StyledBackground, true);
    auto* vhrL = new QHBoxLayout(varsHeaderRow);
    vhrL->setContentsMargins(12, 8, 8, 6);
    vhrL->setSpacing(4);

    auto* varsHeaderLabel = new QLabel("VARIABLES");
    varsHeaderLabel->setObjectName("ComposerHeaderLabel");

    auto* varsOpenBtn = new QPushButton;
    varsOpenBtn->setObjectName("SidebarBtn");
    varsOpenBtn->setFixedSize(20, 20);
    varsOpenBtn->setIcon(gui::icons::openExternal());
    varsOpenBtn->setIconSize(QSize(14, 14));
    varsOpenBtn->setCursor(Qt::PointingHandCursor);
    varsOpenBtn->setToolTip("Open vars.fct in editor");
    connect(varsOpenBtn, &QPushButton::clicked, this,
            []() { utils::openSystemFile(BASE_PATH + "/" + VARS_PATH); });

    auto* varsReloadBtn = new QPushButton;
    varsReloadBtn->setObjectName("SidebarBtn");
    varsReloadBtn->setFixedSize(20, 20);
    varsReloadBtn->setIcon(gui::icons::reload());
    varsReloadBtn->setIconSize(QSize(14, 14));
    varsReloadBtn->setCursor(Qt::PointingHandCursor);
    varsReloadBtn->setToolTip("Reload variables from file");
    connect(varsReloadBtn, &QPushButton::clicked, this, &PromptComposerPage::reloadVars);

    vhrL->addWidget(varsHeaderLabel, 1);
    vhrL->addWidget(varsOpenBtn);
    vhrL->addWidget(varsReloadBtn);

    // ---- Workflow / States sidebar section
    // (wfSep removed; the QSplitter handle below replaces it.)
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

    m_wfEditBtnRef = new QPushButton;
    m_wfEditBtnRef->setObjectName("SidebarBtn");
    m_wfEditBtnRef->setFixedSize(20, 20);
    m_wfEditBtnRef->setIcon(gui::icons::openExternal());
    m_wfEditBtnRef->setIconSize(QSize(14, 14));
    m_wfEditBtnRef->setCursor(Qt::PointingHandCursor);
    m_wfEditBtnRef->setToolTip("Workflow Variable Editor");
    connect(m_wfEditBtnRef, &QPushButton::clicked, this,
            [this]() { emit workflowEditorRequested(); });

    m_saveStateBtn = new QPushButton;
    m_saveStateBtn->setObjectName("SidebarBtn");
    m_saveStateBtn->setFixedSize(20, 20);
    m_saveStateBtn->setIcon(gui::icons::plus());
    m_saveStateBtn->setIconSize(QSize(14, 14));
    m_saveStateBtn->setCursor(Qt::PointingHandCursor);
    m_saveStateBtn->setToolTip("Save current state");
    m_saveStateBtn->setVisible(false);
    connect(m_saveStateBtn, &QPushButton::clicked, this, &PromptComposerPage::saveCurrentState);

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
        const QFileInfo fi(path);

        // Copy into data/workflows/ so the data folder is portable across
        // machines - the workflow list stores a relative path that resolves
        // against BASE_PATH instead of the absolute source path.
        const QString workflowsDir = BASE_PATH + "/data/workflows";
        QDir().mkpath(workflowsDir);

        const QString srcCanonical = fi.canonicalFilePath();
        const QString destDirCanonical = QFileInfo(workflowsDir).canonicalFilePath();
        const bool alreadyInWorkflowsDir =
            !destDirCanonical.isEmpty() && !srcCanonical.isEmpty() &&
            srcCanonical.startsWith(destDirCanonical + "/", Qt::CaseInsensitive);

        QString destAbs;
        if (alreadyInWorkflowsDir) {
            destAbs = srcCanonical;
        }
        else {
            // Dedupe filename collisions with " (N)" suffix.
            QString destPath = workflowsDir + "/" + fi.fileName();
            for (int n = 2; QFile::exists(destPath); ++n) {
                destPath = QString("%1/%2 (%3).%4")
                               .arg(workflowsDir, fi.completeBaseName())
                               .arg(n)
                               .arg(fi.suffix());
            }
            if (!QFile::copy(path, destPath)) return;
            destAbs = destPath;
        }

        const QString relPath = QDir(BASE_PATH).relativeFilePath(destAbs);

        for (const auto& wf : m_wfManager->files())
            if (wf.path == relPath) return;

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const QString newId = QString::number(now);
        core::WorkflowFile wf;
        wf.id = newId;
        wf.name = fi.completeBaseName();
        wf.path = relPath;
        wf.createdAt = now;
        // Prepend so newest shows at top; shift selectedIndex to follow.
        m_wfManager->files().prepend(wf);
        if (m_wfManager->selectedIndex() < 0)
            m_wfManager->setSelectedIndex(0);
        else
            m_wfManager->setSelectedIndex(m_wfManager->selectedIndex() + 1);
        m_wfManager->saveToFile(m_wfSavePath);
        rebuildWorkflowList();
    });

    connect(m_wfList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (!m_wfManager || item->data(Qt::UserRole).isNull()) return;
        const int newIdx = item->data(Qt::UserRole).toInt();
        if (newIdx == m_wfManager->selectedIndex()) return;
        m_wfManager->setSelectedIndex(newIdx);
        m_wfManager->saveToFile(m_wfSavePath);
        captureUndoSnapshot();
        rebuildWorkflowList();
        emit workflowVarsChanged();
    });

    connect(m_wfList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (!m_wfManager) return;
        QListWidgetItem* item = m_wfList->itemAt(pos);
        if (!item || item->data(Qt::UserRole).isNull()) return;
        const int idx = item->data(Qt::UserRole).toInt();
        if (idx < 0 || idx >= m_wfManager->files().size()) return;

        QMenu menu(this);
        menu.addAction("Open file", this, [this, idx]() {
            if (idx >= m_wfManager->files().size()) return;
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_wfManager->files()[idx].path));
        });
        menu.addSeparator();
        menu.addAction("Rename", this, [this, idx]() {
            if (idx >= m_wfManager->files().size()) return;
            bool ok;
            const QString name =
                QInputDialog::getText(this, "Rename Workflow", "Name:", QLineEdit::Normal,
                                      m_wfManager->files()[idx].name, &ok);
            if (!ok || name.trimmed().isEmpty()) return;
            m_wfManager->files()[idx].name = name.trimmed();
            m_wfManager->touch(idx);
            m_wfManager->saveToFile(m_wfSavePath);
            rebuildWorkflowList();
        });
        menu.addAction("Remove", this, [this, idx]() {
            if (idx >= m_wfManager->files().size()) return;
            m_wfManager->files().removeAt(idx);
            int sel = m_wfManager->selectedIndex();
            const int sz = m_wfManager->files().size();
            if (sz == 0)
                m_wfManager->setSelectedIndex(-1);
            else if (sel >= sz)
                m_wfManager->setSelectedIndex(sz - 1);
            m_wfManager->saveToFile(m_wfSavePath);
            rebuildWorkflowList();
        });
        menu.exec(m_wfList->mapToGlobal(pos));
    });

    rebuildWorkflowList();

    // ---- States list
    m_statesList = new StatesListWidget;
    m_statesList->setObjectName("StatesList");
    m_statesList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_statesList->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));
    m_statesList->setIconSize(QSize(64, 64));
    m_statesList->installEventFilter(this);

    // Visual row != model index when filtered; UserRole stores the model index.
    auto modelIdxOf = [this](QListWidgetItem* item) -> int {
        if (!item) return -1;
        const QVariant v = item->data(Qt::UserRole);
        if (!v.isValid()) return -1;
        return v.toInt();
    };

    connect(m_statesList, &StatesListWidget::imageDroppedOnRow, this,
            [this, modelIdxOf](int listRow, const QString& srcPath) {
                const int row = modelIdxOf(m_statesList->item(listRow));
                if (row < 0 || row >= m_stateManager.states().size()) return;
                core::SavedState& state = m_stateManager.states()[row];
                const QString stateDir = m_statesDir + "/" + state.id;
                QDir().mkpath(stateDir);
                // Drop any existing preview regardless of extension.
                if (!state.previewImagePath.isEmpty() && QFile::exists(state.previewImagePath))
                    QFile::remove(state.previewImagePath);
                const QString dest = stateDir + "/preview.png";
                if (!saveStatePreview(srcPath, dest)) return;
                state.previewImagePath = dest;
                state.createdAt = QDateTime::currentMSecsSinceEpoch();
                if (row != 0) m_stateManager.states().move(row, 0);
                m_stateManager.saveToDir(m_statesDir);
                rebuildStatesList();
            });

    connect(m_statesList, &QListWidget::itemClicked, this,
            [this, modelIdxOf](QListWidgetItem* item) {
                const int row = modelIdxOf(item);
                if (row >= 0 && row < m_stateManager.states().size()) {
                    m_freezeNextRebuild = true;
                    restoreState(m_stateManager.states()[row]);
                }
            });

    connect(m_statesList, &QListWidget::itemEntered, this,
            [this](QListWidgetItem* item) { showStatePreview(m_statesList->row(item)); });

    m_statesList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_statesList, &QListWidget::customContextMenuRequested, this,
            [this, modelIdxOf](const QPoint& pos) {
                QListWidgetItem* item = m_statesList->itemAt(pos);
                if (!item) return;
                const int row = modelIdxOf(item);
                if (row < 0 || row >= m_stateManager.states().size()) return;

                QMenu menu;
                QAction* openAct = menu.addAction("Open state file");
                menu.addSeparator();
                QAction* overwriteAct = menu.addAction("Overwrite with current");
                QAction* renameAct = menu.addAction("Rename");
                QAction* deleteAct = menu.addAction("Delete");
                QAction* chosen = menu.exec(m_statesList->mapToGlobal(pos));

                if (chosen == openAct) {
                    const QString statePath =
                        m_statesDir + "/" + m_stateManager.states()[row].id + "/state.json";
                    QDesktopServices::openUrl(QUrl::fromLocalFile(statePath));
                }
                else if (chosen == overwriteAct) {
                    overwriteState(row);
                }
                else if (chosen == renameAct) {
                    bool ok;
                    const QString name =
                        QInputDialog::getText(this, "Rename State", "Name:", QLineEdit::Normal,
                                              m_stateManager.states()[row].name, &ok);
                    if (!ok || name.trimmed().isEmpty()) return;
                    m_stateManager.states()[row].name = name.trimmed();
                    m_stateManager.states()[row].createdAt =
                        QDateTime::currentMSecsSinceEpoch();
                    if (row != 0) m_stateManager.states().move(row, 0);
                    m_stateManager.saveToDir(m_statesDir);
                    rebuildStatesList();
                }
                else if (chosen == deleteAct) {
                    const QString stateDir = m_statesDir + "/" + m_stateManager.states()[row].id;
                    QDir(stateDir).removeRecursively();
                    m_stateManager.states().removeAt(row);
                    m_stateManager.saveToDir(m_statesDir);
                    hideStatePreview();
                    rebuildStatesList();
                }
            });

    // Tab toggle
    connect(wfTabBtn, &QPushButton::clicked, this, [this, wfTabBtn, statesTabBtn]() {
        wfTabBtn->setChecked(true);
        statesTabBtn->setChecked(false);
        m_wfStateStack->setCurrentIndex(0);
        m_wfEditBtnRef->setVisible(true);
        m_saveStateBtn->setVisible(false);
        hideStatePreview();
    });
    connect(statesTabBtn, &QPushButton::clicked, this, [this, wfTabBtn, statesTabBtn]() {
        statesTabBtn->setChecked(true);
        wfTabBtn->setChecked(false);
        m_wfStateStack->setCurrentIndex(1);
        m_wfEditBtnRef->setVisible(false);
        m_saveStateBtn->setVisible(true);
    });

    auto* wfPage = new QWidget;
    auto* wfPageL = new QVBoxLayout(wfPage);
    wfPageL->setContentsMargins(0, 0, 0, 0);
    wfPageL->setSpacing(2);
    m_wfFilter = new QLineEdit;
    m_wfFilter->setObjectName("ComposerVarEdit");
    m_wfFilter->setPlaceholderText("Filter workflows...");
    m_wfFilter->setClearButtonEnabled(true);
    wfPageL->addWidget(m_wfFilter);
    wfPageL->addWidget(m_wfList, 1);
    connect(m_wfFilter, &QLineEdit::textChanged, this,
            [this](const QString&) { rebuildWorkflowList(); });

    auto* statesPage = new QWidget;
    auto* statesPageL = new QVBoxLayout(statesPage);
    statesPageL->setContentsMargins(0, 0, 0, 0);
    statesPageL->setSpacing(2);
    m_statesFilter = new QLineEdit;
    m_statesFilter->setObjectName("ComposerVarEdit");
    m_statesFilter->setPlaceholderText("Filter states...");
    m_statesFilter->setClearButtonEnabled(true);
    statesPageL->addWidget(m_statesFilter);
    statesPageL->addWidget(m_statesList, 1);
    connect(m_statesFilter, &QLineEdit::textChanged, this,
            [this](const QString&) { rebuildStatesList(); });

    m_wfStateStack = new QStackedWidget;
    m_wfStateStack->addWidget(wfPage);     // 0
    m_wfStateStack->addWidget(statesPage); // 1
    // Height is driven by the sidebar splitter; minimum keeps tabs visible
    // when the user collapses toward the top.
    m_wfStateStack->setMinimumHeight(80);

    // Floating preview popup for state images
    m_statesPreviewPopup =
        new QLabel(this, Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    m_statesPreviewPopup->setObjectName("StatesPreviewPopup");
    m_statesPreviewPopup->setAttribute(Qt::WA_ShowWithoutActivating);
    m_statesPreviewPopup->setAttribute(Qt::WA_StyledBackground, true);
    m_statesPreviewPopup->setAlignment(Qt::AlignCenter);
    m_statesPreviewPopup->hide();

    // Rules section + WF/States section live inside a vertical QSplitter so
    // the divider between them is draggable. The vars section stays pinned
    // below the splitter at its natural height.
    auto* rulesSection = new QWidget;
    auto* rulesSectionL = new QVBoxLayout(rulesSection);
    rulesSectionL->setContentsMargins(0, 0, 0, 0);
    rulesSectionL->setSpacing(0);
    rulesSectionL->addWidget(rulesHeaderRow);
    rulesSectionL->addWidget(rulesScroll, 1);

    auto* wfStatesSection = new QWidget;
    auto* wfStatesSectionL = new QVBoxLayout(wfStatesSection);
    wfStatesSectionL->setContentsMargins(0, 0, 0, 0);
    wfStatesSectionL->setSpacing(0);
    wfStatesSectionL->addWidget(wfHeaderRow);
    wfStatesSectionL->addWidget(m_wfStateStack, 1);

    auto* sidebarSplit = new QSplitter(Qt::Vertical);
    sidebarSplit->setObjectName("ComposerSidebarSplit");
    sidebarSplit->setHandleWidth(5);
    sidebarSplit->setChildrenCollapsible(false);
    sidebarSplit->addWidget(rulesSection);
    sidebarSplit->addWidget(wfStatesSection);
    sidebarSplit->setStretchFactor(0, 1);
    sidebarSplit->setStretchFactor(1, 0);
    // Initial split: WF/states starts ~340px so ~4 thumbnail rows fit. Rules
    // takes the remainder.
    sidebarSplit->setSizes({400, 340});

    auto* sidebar = new QWidget;
    sidebar->setObjectName("ComposerSidebar");
    sidebar->setFixedWidth(330);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);
    sidebarLayout->addWidget(sidebarSplit, 1);
    sidebarLayout->addWidget(varsSep);
    sidebarLayout->addWidget(varsHeaderRow);
    sidebarLayout->addWidget(m_varsContainer);

    // ---- Root layout
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(mainArea, 1);
    root->addWidget(sidebar);

    // ---- Category nav panel (floating, top-right)
    auto* navPanel = new CategoryNavPanel(this);
    m_categoryNav = navPanel;
    navPanel->onCategoryClicked = [this](const QString& displayName) {
        auto it = m_groupHeaders.find(displayName);
        if (it != m_groupHeaders.end() && it.value()) {
            const int y = it.value()->mapTo(m_groupsScroll->widget(), QPoint(0, 0)).y();
            m_groupsScroll->scrollToY(y);
        }
    };

    // ---- Clear button (floating, just left of nav panel)
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

        captureUndoSnapshot();
        repush();
    });

    // ---- Preview image label (floating, bottom-right)
    static constexpr int PreviewSize = 200;
    m_previewLabel = new PreviewClickLabel(this);
    m_previewLabel->setObjectName("ComposerPreviewLabel");
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setFixedSize(PreviewSize, PreviewSize);
    m_previewLabel->hide(); // shown on first image arrival

    // ---- Control bar (floating below preview)
    m_controlBar = new QWidget(this);
    m_controlBar->setObjectName("ComposerControlBar");
    m_controlBar->setAttribute(Qt::WA_StyledBackground, true);
    // 25 px buttons + 4 px top/bottom margin; no slack to avoid uneven centering.
    m_controlBar->setFixedHeight(33);

    m_copyBtn = new QPushButton("Copy prompt", m_controlBar);
    m_copyBtn->setObjectName("ComposerCopyBtn");
    m_copyBtn->setCursor(Qt::PointingHandCursor);
    connect(m_copyBtn, &QPushButton::clicked, this,
            [this]() { QGuiApplication::clipboard()->setText(currentPromptString(false)); });

    m_undoBtn = new QPushButton("↶", m_controlBar);
    m_undoBtn->setObjectName("ComposerUndoBtn");
    m_undoBtn->setFixedSize(25, 25);
    m_undoBtn->setCursor(Qt::PointingHandCursor);
    m_undoBtn->setToolTip("Undo (Ctrl+Z)");
    m_undoBtn->setEnabled(false);
    connect(m_undoBtn, &QPushButton::clicked, this, &PromptComposerPage::undo);

    m_redoBtn = new QPushButton("↷", m_controlBar);
    m_redoBtn->setObjectName("ComposerRedoBtn");
    m_redoBtn->setFixedSize(25, 25);
    m_redoBtn->setCursor(Qt::PointingHandCursor);
    m_redoBtn->setToolTip("Redo (Ctrl+Shift+Z)");
    m_redoBtn->setEnabled(false);
    connect(m_redoBtn, &QPushButton::clicked, this, &PromptComposerPage::redo);

    auto* undoSc = new QShortcut(QKeySequence::Undo, this);
    undoSc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(undoSc, &QShortcut::activated, this, &PromptComposerPage::undo);

    auto* redoSc = new QShortcut(QKeySequence("Ctrl+Shift+Z"), this);
    redoSc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(redoSc, &QShortcut::activated, this, &PromptComposerPage::redo);

    m_runBtn = new QPushButton("Run", m_controlBar);
    m_runBtn->setObjectName("ComposerRunBtn");
    m_runBtn->setIcon(gui::icons::play(14, QColor(0x77, 0xaa, 0xdd)));
    m_runBtn->setIconSize(QSize(12, 12));
    m_runBtn->setCursor(Qt::PointingHandCursor);

    m_promptCountSpin = new QSpinBox(m_controlBar);
    m_promptCountSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_promptCountSpin->setObjectName("ComposerCountSpin");
    m_promptCountSpin->setRange(1, 99);
    m_promptCountSpin->setValue(1);
    m_promptCountSpin->setFixedWidth(40);

    m_interruptBtn = new QPushButton(m_controlBar);
    m_interruptBtn->setObjectName("ComposerInterruptBtn");
    m_interruptBtn->setFixedSize(25, 25);
    m_interruptBtn->setIcon(gui::icons::stopSquare(14, QColor(0xee, 0x44, 0x44)));
    m_interruptBtn->setIconSize(QSize(11, 11));
    m_interruptBtn->setCursor(Qt::PointingHandCursor);
    m_interruptBtn->setToolTip("Interrupt");

    m_undefinedToggleBtn = new QPushButton("?", m_controlBar);
    m_undefinedToggleBtn->setObjectName("ComposerUndefToggle");
    m_undefinedToggleBtn->setCheckable(true);
    m_undefinedToggleBtn->setCursor(Qt::PointingHandCursor);
    m_undefinedToggleBtn->setToolTip("Show only tags without facet definitions");
    connect(m_undefinedToggleBtn, &QPushButton::toggled, this, [this](bool on) {
        m_undefinedOnly = on;
        applyTagFilter();
    });

    auto* barLayout = new QHBoxLayout(m_controlBar);
    barLayout->setContentsMargins(4, 4, 4, 4);
    barLayout->setSpacing(4);
    barLayout->addWidget(m_undefinedToggleBtn);
    barLayout->addWidget(m_copyBtn);
    barLayout->addWidget(m_undoBtn);
    barLayout->addWidget(m_redoBtn);
    barLayout->addWidget(m_runBtn, 1);
    barLayout->addWidget(m_promptCountSpin);
    barLayout->addWidget(m_interruptBtn);
    m_controlBar->adjustSize();

    connect(m_runBtn, &QPushButton::clicked, this,
            [this]() { emit runRequested(m_promptCountSpin->value()); });
    connect(m_interruptBtn, &QPushButton::clicked, this, [this]() { emit interruptRequested(); });

    // Starts at 0 so first preview fades in; popout open/close also drives it.
    m_previewInsetFx = new QGraphicsOpacityEffect(m_previewLabel);
    m_previewInsetFx->setOpacity(0.0);
    m_previewLabel->setGraphicsEffect(m_previewInsetFx);
    m_previewInsetFade = new QPropertyAnimation(m_previewInsetFx, "opacity", this);
    m_previewInsetFade->setDuration(350);
    m_previewInsetFade->setEasingCurve(QEasingCurve::InOutSine);

    // ---- Open popout on preview click
    connect(m_previewLabel, &PreviewClickLabel::clicked, this, [this]() {
        if (!m_popout) {
            auto* popout =
                new PreviewPopoutWindow(nullptr); // null parent: real top-level for FancyZones
            popout->setAttribute(Qt::WA_DeleteOnClose);
            m_popout = popout;
            m_popout->installEventFilter(this);
            connect(m_popout, &QObject::destroyed, this, [this]() {
                m_popout = nullptr;
                // Skip fade-back if nothing ever showed; setPreviewImage
                // will reveal the inset when content arrives.
                if (m_previewLabel->isVisible()) fadePreviewInset(1.0);
            });
            if (!m_tempFolder.isEmpty()) popout->setTempFolder(m_tempFolder);
            if (!m_outputFolderPattern.isEmpty())
                popout->setOutputFolder(resolveDatePattern(m_outputFolderPattern));
            popout->setActiveCount(m_lastComfyActive);
            if (m_lastComfyStep > 0 && m_lastComfyTotal > 0)
                popout->setProgress(m_lastComfyStep, m_lastComfyTotal);

            // Forward shortcut intents so AppMainWindow/ComfyUI see them
            // the same as from the main window.
            connect(popout, &PreviewPopoutWindow::runRequested, this,
                    [this]() { emit runRequested(m_promptCountSpin->value()); });
            connect(popout, &PreviewPopoutWindow::interruptRequested, this,
                    &PromptComposerPage::interruptRequested);
            connect(popout, &PreviewPopoutWindow::clearPendingRequested, this,
                    &PromptComposerPage::clearPendingRequested);
        }
        if (!m_currentPix.isNull())
            static_cast<PreviewPopoutWindow*>(m_popout)->setImage(m_currentPix);
        m_popout->show();
        m_popout->raise();
        m_popout->activateWindow();
        fadePreviewInset(0.0); // popout takes over; hide the inset
    });

    // ---- Wire pipeline
    connect(m_pipeline, &PromptPipeline::pipelineReady, this, &PromptComposerPage::onPipelineReady);

    rebuildRulesSidebar();
}

// ---- showEvent / eventFilter

void PromptComposerPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    repositionFloats();
    // Workflow editor may have reordered files via touch(); resync here.
    rebuildWorkflowList();
}

bool PromptComposerPage::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_groupsScroll && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Down || ke->key() == Qt::Key_Up) {
            if (m_tagRowWidgets.isEmpty()) return false;
            int next = m_selectedRowIdx;
            if (next < 0)
                next = (ke->key() == Qt::Key_Down) ? 0 : m_tagRowWidgets.size() - 1;
            else
                next += (ke->key() == Qt::Key_Down) ? 1 : -1;
            next = std::clamp(next, 0, int(m_tagRowWidgets.size()) - 1);
            setSelectedRow(next);
            return true;
        }

        // Delete on the selected row removes the tag (same as the row's
        // ✕ button). Skip Injected rows (no row-level "active" entry to
        // delete) - their _tag isn't in m_activeTagSet.
        const auto modsRaw = ke->modifiers() & ~Qt::ShiftModifier;
        if (modsRaw == Qt::NoModifier && ke->key() == Qt::Key_Delete &&
            m_selectedRowIdx >= 0 && m_selectedRowIdx < m_tagRowWidgets.size()) {
            QWidget* row = m_tagRowWidgets[m_selectedRowIdx];
            QString activeKey;
            if (auto* edit = row->findChild<QLineEdit*>())
                activeKey = edit->property("_tag").toString();
            if (!activeKey.isEmpty() && m_activeTagSet.contains(activeKey)) {
                m_activeTags.removeOne(activeKey);
                m_activeTagSet.remove(activeKey);
                m_tagWeights.remove(activeKey);
                m_deactivatedTags.remove(activeKey);
                m_deactivatedCategory.remove(activeKey);
                captureUndoSnapshot();
                queueRepush();
                return true;
            }
        }

        // Type-to-edit: any printable char or Backspace on the focused
        // list focuses the selected row's editable QLineEdit and replays
        // the keystroke. Skip when modifiers are pressed so app shortcuts
        // (Ctrl+C, Shift+E run, etc.) still reach their handlers.
        const auto mods = ke->modifiers() & ~Qt::ShiftModifier;
        const bool isPrintable = !ke->text().isEmpty() && ke->text()[0].isPrint();
        const bool isEditKey = ke->key() == Qt::Key_Backspace;
        if (mods == Qt::NoModifier && (isPrintable || isEditKey) && m_selectedRowIdx >= 0 &&
            m_selectedRowIdx < m_tagRowWidgets.size()) {
            QWidget* row = m_tagRowWidgets[m_selectedRowIdx];
            if (auto* edit = row->findChild<QLineEdit*>("ComposerTagEdit")) {
                edit->setFocus(Qt::OtherFocusReason);
                // Printable replaces the tag (selectAll then type); Backspace/
                // Delete just trims from the end.
                if (isPrintable)
                    edit->selectAll();
                else
                    edit->setCursorPosition(edit->text().size());
                QApplication::sendEvent(edit, ke);
                return true;
            }
        }
    }
    if (obj == m_statesList && event->type() == QEvent::Leave) {
        hideStatePreview();
        return false;
    }
    if (obj == m_popout) {
        if (event->type() == QEvent::Show) {
            m_previewLabel->hide();
        }
        else if (event->type() == QEvent::Hide) {
            if (!m_currentPix.isNull()) {
                m_previewLabel->show();
                repositionFloats();
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

// ---- Preview & control bar

void PromptComposerPage::setPreviewImage(const QImage& image)
{
    if (image.isNull()) return;
    m_currentPix = QPixmap::fromImage(image);
    // Hardcode size; size() can be 0x0 before first layout.
    m_previewLabel->setPixmap(
        m_currentPix.scaled(QSize(200, 200), Qt::KeepAspectRatio, Qt::SmoothTransformation));

    const bool popoutOpen = m_popout && m_popout->isVisible();
    if (!popoutOpen) {
        m_previewLabel->show();
        m_previewLabel->raise();
        fadePreviewInset(1.0); // first show fades 0->1; later calls no-op
    }
    repositionFloats();

    if (popoutOpen) static_cast<PreviewPopoutWindow*>(m_popout)->setImage(m_currentPix);
}

void PromptComposerPage::fadePreviewInset(qreal target)
{
    if (!m_previewInsetFx || !m_previewInsetFade) return;
    if (qFuzzyCompare(m_previewInsetFx->opacity(), target)) return;
    m_previewInsetFade->stop();
    m_previewInsetFade->setStartValue(m_previewInsetFx->opacity());
    m_previewInsetFade->setEndValue(target);
    m_previewInsetFade->start();
}

void PromptComposerPage::triggerRun()
{
    emit runRequested(m_promptCountSpin->value());
}

void PromptComposerPage::setOutputFolderPattern(const QString& pattern)
{
    m_outputFolderPattern = pattern;
    if (m_popout)
        static_cast<PreviewPopoutWindow*>(m_popout)->setOutputFolder(resolveDatePattern(pattern));
}

void PromptComposerPage::setComfyProgress(int step, int total)
{
    m_lastComfyStep = step;
    m_lastComfyTotal = total;
    if (m_popout) static_cast<PreviewPopoutWindow*>(m_popout)->setProgress(step, total);
}

void PromptComposerPage::setComfyActiveCount(int count)
{
    m_lastComfyActive = count;
    if (m_popout) static_cast<PreviewPopoutWindow*>(m_popout)->setActiveCount(count);
}

void PromptComposerPage::setTempFolder(const QString& folder)
{
    m_tempFolder = folder;
    if (m_popout) static_cast<PreviewPopoutWindow*>(m_popout)->setTempFolder(folder);
}

void PromptComposerPage::repositionFloats()
{
    constexpr int sidebarW = 220;
    constexpr int marginR = 160;
    constexpr int marginT = 42;
    constexpr int marginB = 12;
    constexpr int gap = 4;
    const int right = width() - sidebarW - marginR;
    int bottom = height() - marginB;

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

    m_controlBar->move(right - m_controlBar->width(), bottom - m_controlBar->height());
    bottom -= m_controlBar->height() + gap;

    if (m_previewLabel->isVisible()) {
        m_previewLabel->move(right - m_previewLabel->width(), bottom - m_previewLabel->height());
    }
}

void PromptComposerPage::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    repositionFloats();
}

// ---- Public API

QList<core::EntryPush> PromptComposerPage::dumpActivePushes() const
{
    QList<core::EntryPush> out;
    for (auto it = m_activePushes.cbegin(); it != m_activePushes.cend(); ++it) {
        const int runtimeId = int(quint32(it.key() >> 32));
        const int imageIdx = int(quint32(it.key() & 0xFFFFFFFFLL));
        core::Entry* entry = m_entryModel ? m_entryModel->entryById(runtimeId) : nullptr;
        if (!entry || imageIdx < 0 || imageIdx >= entry->images.size()) continue;
        core::EntryPush ep;
        ep.uuid = entry->uuid;
        ep.imageFileName = entry->images[imageIdx].fileName;
        ep.tags = it.value();
        out << ep;
    }
    return out;
}

int PromptComposerPage::loadActivePushes(const QList<core::EntryPush>& pushes)
{
    m_activePushes.clear();
    int missing = 0;
    for (const core::EntryPush& ep : pushes) {
        core::Entry* entry = m_entryModel ? m_entryModel->entryByUuid(ep.uuid) : nullptr;
        if (!entry) {
            ++missing;
            continue;
        }
        int imageIdx = -1;
        for (int i = 0; i < entry->images.size(); ++i)
            if (entry->images[i].fileName == ep.imageFileName) {
                imageIdx = i;
                break;
            }
        if (imageIdx < 0) {
            ++missing;
            continue;
        }
        const qint64 key = (qint64(entry->id) << 32) | quint32(imageIdx);
        m_activePushes[key] = ep.tags;
    }

    QMap<int, QList<int>> activeGroups;
    for (auto it = m_activePushes.cbegin(); it != m_activePushes.cend(); ++it) {
        activeGroups[int(quint32(it.key() >> 32))].append(int(quint32(it.key() & 0xFFFFFFFFLL)));
    }
    emit activeGroupsChanged(activeGroups);
    return missing;
}

QList<CategoryGroup> PromptComposerPage::bucketForOutput(const QList<PipelineTag>& flat,
                                                         const TagGroupIndex& groups)
{
    QHash<QString, QList<PipelineTag>> buckets;
    for (const PipelineTag& pt : flat) {
        if (pt.result == RuleResult::Deactivated) continue;
        buckets[groups.groupFor(pt.facets)] << pt;
    }

    QList<CategoryGroup> ordered;
    for (const TagGroup& g : groups.groups()) {
        const auto it = buckets.constFind(g.name);
        if (it == buckets.cend() || it.value().isEmpty()) continue;
        ordered << CategoryGroup{g.name, it.value()};
    }
    // Uncategorized goes last.
    const auto unc = buckets.constFind(QString());
    if (unc != buckets.cend() && !unc.value().isEmpty())
        ordered << CategoryGroup{QString(), unc.value()};
    return ordered;
}

QString PromptComposerPage::currentPromptString(bool forJson) const
{
    return PromptPipeline::buildPromptString(bucketForOutput(m_lastResult, m_groups), forJson);
}

QList<QString> PromptComposerPage::currentActiveTags() const
{
    QList<QString> out = m_activeTags;
    QSet<QString> seen(out.begin(), out.end());
    for (const core::PipelineTag& pt : m_lastResult) {
        if (pt.result == core::RuleResult::Injected && !seen.contains(pt.tag)) {
            seen.insert(pt.tag);
            out << pt.tag;
        }
    }
    return out;
}

QString PromptComposerPage::computePromptForTags(const QList<QString>& tags, bool forJson) const
{
    if (!m_pipeline) return {};

    QList<core::CategoryGroup> groups = m_pipeline->evaluate(tags);

    // Apply weights only where they overlap (batch tags usually differ from composer's).
    for (auto& g : groups)
        for (auto& pt : g.tags)
            pt.weight = m_tagWeights.value(weightKeyOf(pt), 1.0f);

    QList<PipelineTag> flat;
    for (const auto& g : groups)
        flat << g.tags;
    return PromptPipeline::buildPromptString(bucketForOutput(flat, m_groups), forJson);
}

QString PromptComposerPage::computePromptWithExtraTags(const QList<QString>& extraTags,
                                                       bool forJson) const
{
    // Composer state first, then extraTags, dedup preserving order.
    QList<QString> merged;
    QSet<QString> seen;
    for (const QString& t : m_activeTags) {
        if (m_deactivatedTags.contains(t)) continue;
        if (seen.contains(t)) continue;
        seen.insert(t);
        merged << t;
    }
    for (const QString& t : extraTags) {
        if (seen.contains(t)) continue;
        seen.insert(t);
        merged << t;
    }
    return computePromptForTags(merged, forJson);
}

int PromptComposerPage::currentPromptCount() const
{
    return m_promptCountSpin ? m_promptCountSpin->value() : 1;
}

void PromptComposerPage::setDanbooruIndex(core::DanbooruIndex* index)
{
    m_danbooruIndex = index;
    m_searchBar->setIndex(index);
}

void PromptComposerPage::setVariableIndex(core::VariableIndex* index)
{
    m_varIndex = index;
    rebuildVarsSidebar();
}

void PromptComposerPage::setWorkflowManager(core::WorkflowManager* wm, const QString& savePath)
{
    m_wfManager = wm;
    m_wfSavePath = savePath;
    rebuildWorkflowList();
}

void PromptComposerPage::loadPipeline(int entryId, int imageIdx, const QList<QString>& tags)
{
    const qint64 key = (qint64(entryId) << 32) | quint32(imageIdx);
    const bool wasActive = m_activePushes.contains(key);

    if (wasActive) {
        // Only this push's owned tags; manual adds + other-push tags
        // aren't in m_activePushes[key], so they survive.
        QSet<QString> otherTags;
        for (auto it = m_activePushes.cbegin(); it != m_activePushes.cend(); ++it) {
            if (it.key() != key) {
                for (const QString& t : it.value())
                    otherTags.insert(t);
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
    }
    else {
        // Claim only newly-contributed tags; shared ones survive un-push.
        QList<QString> claimed;
        for (const QString& tag : tags) {
            if (!m_activeTagSet.contains(tag)) {
                m_activeTags << tag;
                m_activeTagSet.insert(tag);
                claimed << tag;
            }
        }
        m_activePushes[key] = claimed;
    }

    // ---- LoRA: sync activation with push state
    core::Entry* entry = m_entryModel ? m_entryModel->entryById(entryId) : nullptr;
    if (entry && entry->lora.has_value()) {
        const QString uuid = entry->uuid;
        bool loraChanged = false;
        if (!wasActive) {
            if (!m_activeLoraUuids.contains(uuid)) {
                m_activeLoraUuids.append(uuid);
                loraChanged = true;
            }
        }
        else {
            bool stillPushed = false;
            for (auto it = m_activePushes.cbegin(); it != m_activePushes.cend(); ++it) {
                if (int(quint32(it.key() >> 32)) == entryId) {
                    stillPushed = true;
                    break;
                }
            }
            if (!stillPushed && m_activeLoraUuids.removeAll(uuid) > 0) loraChanged = true;
        }
        if (loraChanged) emit loraUuidsRestored(m_activeLoraUuids);
    }

    QMap<int, QList<int>> activeGroups;
    for (auto it = m_activePushes.cbegin(); it != m_activePushes.cend(); ++it) {
        int eid = int(quint32(it.key() >> 32));
        int img = int(quint32(it.key() & 0xFFFFFFFFLL));
        activeGroups[eid].append(img);
    }
    emit activeGroupsChanged(activeGroups);
    captureUndoSnapshot();
    repush();
}

// ---- Entry tag sync

void PromptComposerPage::onEntryTagAdded(int entryId, int imageIdx, const QString& tag)
{
    const qint64 key = (qint64(entryId) << 32) | quint32(imageIdx);
    if (!m_activePushes.contains(key)) return;

    // Mirrors loadPipeline's claim rule: only track tags this push
    // contributes (skip manual adds / other-push tags).
    if (!m_activeTagSet.contains(tag)) {
        m_activeTags << tag;
        m_activeTagSet.insert(tag);
        if (!m_activePushes[key].contains(tag)) m_activePushes[key] << tag;
    }
    // External mutation: don't snapshot, but redo would now diverge.
    clearRedoStack();
    queueRepush();
}

void PromptComposerPage::onEntryTagRemoved(int entryId, int imageIdx, const QString& tag)
{
    const qint64 key = (qint64(entryId) << 32) | quint32(imageIdx);
    if (!m_activePushes.contains(key)) return;

    m_activePushes[key].removeOne(tag);

    // Only drop from active tags if no other active push still references it
    bool stillNeeded = false;
    for (auto it = m_activePushes.cbegin(); it != m_activePushes.cend(); ++it)
        if (it.value().contains(tag)) {
            stillNeeded = true;
            break;
        }

    if (!stillNeeded) {
        m_activeTags.removeOne(tag);
        m_activeTagSet.remove(tag);
        m_tagWeights.remove(tag);
        m_deactivatedTags.remove(tag);
    }
    clearRedoStack();
    queueRepush();
}

void PromptComposerPage::onEntryDeleted(int32_t entryId, const QString& uuid)
{
    QList<qint64> keysToRemove;
    for (auto it = m_activePushes.cbegin(); it != m_activePushes.cend(); ++it)
        if (int(quint32(it.key() >> 32)) == entryId) keysToRemove << it.key();

    const bool loraGone = m_activeLoraUuids.contains(uuid);
    if (keysToRemove.isEmpty() && !loraGone) return;

    // Drop only tags whose last claim was the disappearing entry.
    QSet<QString> stillClaimed;
    for (auto it = m_activePushes.cbegin(); it != m_activePushes.cend(); ++it) {
        if (keysToRemove.contains(it.key())) continue;
        for (const QString& t : it.value())
            stillClaimed.insert(t);
    }
    for (qint64 key : keysToRemove) {
        for (const QString& t : m_activePushes[key]) {
            if (!stillClaimed.contains(t)) {
                m_activeTags.removeOne(t);
                m_activeTagSet.remove(t);
                m_tagWeights.remove(t);
                m_deactivatedTags.remove(t);
            }
        }
        m_activePushes.remove(key);
    }

    if (loraGone) {
        m_activeLoraUuids.removeAll(uuid);
        emit loraUuidsRestored(m_activeLoraUuids);
    }

    QMap<int, QList<int>> activeGroups;
    for (auto it = m_activePushes.cbegin(); it != m_activePushes.cend(); ++it) {
        activeGroups[int(quint32(it.key() >> 32))].append(int(quint32(it.key() & 0xFFFFFFFFLL)));
    }
    emit activeGroupsChanged(activeGroups);

    clearRedoStack();
    repush();
}

void PromptComposerPage::onImageRemoved(int32_t entryId, int imageIdx)
{
    const qint64 removedKey = (qint64(entryId) << 32) | quint32(imageIdx);
    const bool hadPush = m_activePushes.contains(removedKey);

    // Higher-indexed slots for the same entry need their keys shifted down.
    QList<qint64> higherKeys;
    for (auto it = m_activePushes.cbegin(); it != m_activePushes.cend(); ++it) {
        if (int(quint32(it.key() >> 32)) != entryId) continue;
        const int img = int(quint32(it.key() & 0xFFFFFFFFLL));
        if (img > imageIdx) higherKeys << it.key();
    }

    if (!hadPush && higherKeys.isEmpty()) return;

    if (hadPush) {
        // Same claim rule as onEntryDeleted.
        QSet<QString> stillClaimed;
        for (auto it = m_activePushes.cbegin(); it != m_activePushes.cend(); ++it) {
            if (it.key() == removedKey) continue;
            for (const QString& t : it.value()) stillClaimed.insert(t);
        }
        for (const QString& t : m_activePushes[removedKey]) {
            if (!stillClaimed.contains(t)) {
                m_activeTags.removeOne(t);
                m_activeTagSet.remove(t);
                m_tagWeights.remove(t);
                m_deactivatedTags.remove(t);
            }
        }
        m_activePushes.remove(removedKey);
    }

    // Shift ascending so each move lands in a vacated slot.
    std::sort(higherKeys.begin(), higherKeys.end());
    for (qint64 oldKey : higherKeys) {
        const int oldImg = int(quint32(oldKey & 0xFFFFFFFFLL));
        const qint64 newKey = (qint64(entryId) << 32) | quint32(oldImg - 1);
        m_activePushes.insert(newKey, m_activePushes.take(oldKey));
    }

    QMap<int, QList<int>> activeGroups;
    for (auto it = m_activePushes.cbegin(); it != m_activePushes.cend(); ++it) {
        activeGroups[int(quint32(it.key() >> 32))].append(int(quint32(it.key() & 0xFFFFFFFFLL)));
    }
    emit activeGroupsChanged(activeGroups);

    clearRedoStack();
    repush();
}

// ---- Pipeline

void PromptComposerPage::repush()
{
    m_repushPending = false;
    m_freezeNextRebuild = true;
    QList<QString> active;
    for (const QString& t : m_activeTags)
        if (!m_deactivatedTags.contains(t)) active << t;
    m_pipeline->push(active);
}

void PromptComposerPage::queueRepush()
{
    if (m_repushPending) return;
    m_repushPending = true;
    QMetaObject::invokeMethod(this, &PromptComposerPage::repush, Qt::QueuedConnection);
}

void PromptComposerPage::onPipelineReady(QList<core::CategoryGroup> categoryGroups)
{
    // Deleted drops from m_activeTags entirely; Skipped only hides from output.
    QList<QString> deleted;
    for (auto& g : categoryGroups) {
        auto end = std::remove_if(g.tags.begin(), g.tags.end(), [&deleted](const PipelineTag& pt) {
            if (pt.result == RuleResult::Deleted) {
                deleted << pt.tag;
                return true;
            }
            return false;
        });
        g.tags.erase(end, g.tags.end());
    }
    if (!deleted.isEmpty()) {
        for (const QString& tag : deleted) {
            m_activeTags.removeOne(tag);
            m_activeTagSet.remove(tag);
            m_tagWeights.remove(tag);
            m_deactivatedTags.remove(tag);
        }
    }

    for (auto& g : categoryGroups)
        for (auto& pt : g.tags)
            pt.weight = m_tagWeights.value(weightKeyOf(pt), 1.0f);

    QList<PipelineTag> flat;
    for (const auto& g : categoryGroups)
        flat << g.tags;

    // Display-only entries; not in pipeline output.
    for (const QString& tag : m_activeTags) {
        if (m_deactivatedTags.contains(tag)) {
            PipelineTag pt;
            pt.tag = tag;
            pt.result = RuleResult::Deactivated;
            flat << pt;
        }
    }

    m_lastResult = flat;
    applyTagFilter();
}

// ---- Groups display

void PromptComposerPage::applyTagFilter()
{
    // Any rebuild path (repush, undefined toggle, debounce slot itself) lands
    // here, so cancel a pending search-bar debounce - otherwise typing-then-
    // Enter fires both the repush fade and the debounce fade in sequence.
    if (m_filterDebounceTimer) m_filterDebounceTimer->stop();

    if (m_filterQuery.isEmpty() && !m_undefinedOnly) {
        rebuildGroupsDisplay(m_lastResult);
        return;
    }
    auto isUndefined = [](const PipelineTag& pt) {
        return pt.result == RuleResult::NoFacets ||
               (pt.result == RuleResult::Injected && pt.facets.isEmpty());
    };
    // Display group name a tag will land under in rebuildGroupsDisplay.
    // Mirrored from applyGroupsRebuild so search and display stay in sync.
    auto displayGroupOf = [this](const PipelineTag& pt) -> QString {
        if (pt.result == RuleResult::Deactivated) return QStringLiteral("Deactivated");
        const QString g = m_groups.groupFor(pt.facets);
        return g.isEmpty() ? QStringLiteral("Uncategorized") : g;
    };

    QList<PipelineTag> filtered;
    for (const PipelineTag& pt : m_lastResult) {
        if (m_undefinedOnly && !isUndefined(pt)) continue;
        if (!m_filterQuery.isEmpty()) {
            // Substring match (case-insensitive) so "shirt" finds "black
            // shirt" - same shape as the facet editor's filter.
            const bool matchTag = pt.tag.contains(m_filterQuery, Qt::CaseInsensitive);
            const bool matchSrc = !pt.sourceTag.isEmpty() &&
                                  pt.sourceTag.contains(m_filterQuery, Qt::CaseInsensitive);
            const bool matchGroup =
                displayGroupOf(pt).contains(m_filterQuery, Qt::CaseInsensitive);
            if (!matchTag && !matchSrc && !matchGroup) continue;
        }
        filtered << pt;
    }
    rebuildGroupsDisplay(filtered);
}

void PromptComposerPage::rebuildGroupsDisplay(const QList<PipelineTag>& flat)
{
    const bool fade = m_freezeNextRebuild;
    m_freezeNextRebuild = false;

    if (!fade) {
        applyGroupsRebuild(flat);
        return;
    }

    // Snap invisible, swap, fade back in.
    if (m_mainStackFade->state() == QAbstractAnimation::Running)
        m_mainStackFade->stop();
    m_mainStackFx->setOpacity(0.0);
    applyGroupsRebuild(flat);
    m_mainStackFade->setStartValue(0.0);
    m_mainStackFade->setEndValue(1.0);
    m_mainStackFade->start();
}

void PromptComposerPage::applyGroupsRebuild(const QList<PipelineTag>& flat)
{
    while (m_groupsLayout->count() > 0) {
        QLayoutItem* item = m_groupsLayout->takeAt(0);
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }

    m_groupHeaders.clear();
    m_tagRowWidgets.clear();
    m_selectedRowIdx = -1;

    // Count off m_lastResult (not flat) so the nav badge stays accurate
    // when a search query has narrowed the view.
    auto isUndefined = [](const PipelineTag& pt) {
        return pt.result == RuleResult::NoFacets ||
               (pt.result == RuleResult::Injected && pt.facets.isEmpty());
    };
    int undefTotal = 0;
    for (const PipelineTag& pt : m_lastResult)
        if (isUndefined(pt)) ++undefTotal;

    // Defining the last undefined tag while filtered would strand the user
    // on an empty view; release the filter (setChecked re-enters via toggled).
    if (m_undefinedToggleBtn && undefTotal == 0 && m_undefinedToggleBtn->isChecked()) {
        m_undefinedToggleBtn->setChecked(false);
        return;
    }

    if (m_undefinedToggleBtn) {
        m_undefinedToggleBtn->setText(undefTotal > 0 ? QString("?  %1").arg(undefTotal) : "?");
        m_undefinedToggleBtn->setEnabled(undefTotal > 0 || m_undefinedOnly);
        m_undefinedToggleBtn->setProperty("warn", undefTotal > 0);
        m_undefinedToggleBtn->style()->unpolish(m_undefinedToggleBtn);
        m_undefinedToggleBtn->style()->polish(m_undefinedToggleBtn);
    }

    if (flat.isEmpty()) {
        m_mainStack->setCurrentIndex(0);
        if (m_categoryNav) static_cast<CategoryNavPanel*>(m_categoryNav)->updateCategories({});
        return;
    }
    m_mainStack->setCurrentIndex(1);

    auto addSection = [this](const QString& displayName, const QList<PipelineTag>& tags,
                             QStringList& navNames) {
        auto* header = new QLabel(displayName);
        header->setObjectName("ComposerGroupHeader");
        m_groupsLayout->addWidget(header);
        m_groupHeaders[displayName] = header;
        navNames << displayName;
        for (const PipelineTag& pt : tags) {
            QWidget* row = makeTagRow(pt);
            m_groupsLayout->addWidget(row);
            m_tagRowWidgets << row;
        }
        auto* spacer = new QWidget;
        spacer->setFixedHeight(6);
        m_groupsLayout->addWidget(spacer);
    };

    // Display bucketing - includes deactivated tags so they keep their
    // original category section. Output-side bucketForOutput still filters
    // them, so prompt strings are unaffected.
    QHash<QString, QList<PipelineTag>> displayBuckets;
    for (const PipelineTag& pt : flat) {
        const QString cat = (pt.result == RuleResult::Deactivated)
                                ? m_deactivatedCategory.value(pt.tag)
                                : m_groups.groupFor(pt.facets);
        displayBuckets[cat] << pt;
    }

    QStringList navNames;
    for (const TagGroup& g : m_groups.groups()) {
        const auto it = displayBuckets.constFind(g.name);
        if (it == displayBuckets.cend() || it.value().isEmpty()) continue;
        addSection(g.name, it.value(), navNames);
    }
    const auto unc = displayBuckets.constFind(QString());
    if (unc != displayBuckets.cend() && !unc.value().isEmpty())
        addSection("Uncategorized", unc.value(), navNames);

    m_groupsLayout->addStretch();

    // Undefined tags always bucket into Uncategorized (no facets -> no category).
    QHash<QString, int> undefinedByCategory;
    if (undefTotal > 0) undefinedByCategory["Uncategorized"] = undefTotal;
    if (m_categoryNav)
        static_cast<CategoryNavPanel*>(m_categoryNav)->updateCategories(navNames,
                                                                       undefinedByCategory);
}

void PromptComposerPage::setSelectedRow(int idx)
{
    auto repolish = [](QWidget* w) {
        w->style()->unpolish(w);
        w->style()->polish(w);
        w->update();
    };
    if (m_selectedRowIdx >= 0 && m_selectedRowIdx < m_tagRowWidgets.size()) {
        QWidget* old = m_tagRowWidgets[m_selectedRowIdx];
        old->setProperty("selected", false);
        repolish(old);
    }
    m_selectedRowIdx = idx;
    if (m_selectedRowIdx >= 0 && m_selectedRowIdx < m_tagRowWidgets.size()) {
        QWidget* row = m_tagRowWidgets[m_selectedRowIdx];
        row->setProperty("selected", true);
        repolish(row);
        if (m_groupsScroll) m_groupsScroll->ensureWidgetVisible(row, 0, 24);
    }
}

// ---- Tag row

void PromptComposerPage::renamePushTag(const QString& oldKey, const QString& newKey)
{
    for (auto it = m_activePushes.begin(); it != m_activePushes.end(); ++it) {
        QList<QString>& v = it.value();
        const int idx = v.indexOf(oldKey);
        if (idx < 0) continue;
        if (newKey.isEmpty() || v.contains(newKey))
            v.removeAt(idx);
        else
            v[idx] = newKey;
    }
}

void PromptComposerPage::replaceTagVariable(const QString& oldKey, const QString& newVarName)
{
    const int i = m_activeTags.indexOf(oldKey);
    if (i < 0) return;

    static const QRegularExpression varRe(R"(\$([A-Za-z0-9_]+)\$)");
    QString newKey = oldKey;

    if (newVarName.isEmpty()) {
        newKey = VariableIndex::stripVariables(newKey);
    }
    else {
        const QString token = "$" + newVarName + "$";
        // Replace every $name$ so the badge (which collapses vars to one pill) stays in sync.
        newKey.replace(varRe, token);
    }

    newKey = newKey.trimmed();
    if (newKey.isEmpty() || newKey == oldKey) return;

    const bool collide = m_activeTagSet.contains(newKey);
    if (collide) {
        // Already present; drop instead of duping.
        m_activeTags.removeAt(i);
        m_activeTagSet.remove(oldKey);
        m_tagWeights.remove(oldKey);
    }
    else {
        m_activeTags[i] = newKey;
        m_activeTagSet.remove(oldKey);
        m_activeTagSet.insert(newKey);
        // Migrate weight so the variable swap doesn't silently drop it.
        if (m_tagWeights.contains(oldKey)) m_tagWeights[newKey] = m_tagWeights.take(oldKey);
    }
    m_deactivatedTags.remove(oldKey);
    renamePushTag(oldKey, collide ? QString() : newKey);

    captureUndoSnapshot();
    queueRepush();
}

QHash<QAction*, QString> PromptComposerPage::addQuickFacetActions(QMenu& menu) const
{
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
    if (!any) return {};

    menu.addSeparator();
    QHash<QAction*, QString> out;
    for (const auto& e : entries) {
        if (e.second.isEmpty()) continue;
        QAction* a = menu.addAction(QString("Quick add as %1 (%2)").arg(e.first, e.second));
        out.insert(a, e.second);
    }
    return out;
}

QWidget* PromptComposerPage::makeTagRow(const PipelineTag& pt)
{
    const bool hasVar = !pt.sourceTag.isEmpty();
    const bool isDeactivated = (pt.result == RuleResult::Deactivated);
    const bool inActive = (pt.result != RuleResult::Injected);
    const bool editable = !hasVar && !isDeactivated &&
                          (pt.result == RuleResult::Include || pt.result == RuleResult::NoFacets ||
                           pt.result == RuleResult::Flagged);

    const QString activeKey = hasVar ? pt.sourceTag : pt.tag;

    auto* row = new QWidget;
    row->setObjectName("ComposerTagRow");

    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(20, 1, 8, 1);
    rl->setSpacing(8);

    auto* dot = new QWidget;
    dot->setFixedSize(6, 6);
    dot->setStyleSheet(QString("background:%1;border-radius:3px;").arg(dotColorFor(pt.result)));
    rl->addWidget(dot, 0, Qt::AlignVCenter);

    auto* tagEdit = new QLineEdit(pt.tag);
    tagEdit->setObjectName(editable ? "ComposerTagEdit" : "ComposerTagReadOnly");
    tagEdit->setReadOnly(!editable);
    tagEdit->setProperty("_tag", activeKey);

    if (pt.result == RuleResult::Skipped || pt.result == RuleResult::Replaced ||
        pt.result == RuleResult::Deactivated) {
        QFont f = tagEdit->font();
        f.setStrikeOut(true);
        tagEdit->setFont(f);
    }

    if (editable) {
        // Danbooru-style autocomplete popup on the inline edit. Picking a
        // suggestion sets the text and clears focus, so the rename below
        // runs unchanged.
        if (m_danbooruIndex) new TagLineAutocomplete(tagEdit, m_danbooruIndex, tagEdit);
        connect(tagEdit, &QLineEdit::editingFinished, this, [this, tagEdit]() {
            const QString oldTag = tagEdit->property("_tag").toString();
            const QString newTag = tagEdit->text().trimmed();
            if (newTag.isEmpty() || newTag == oldTag) return;
            const int i = m_activeTags.indexOf(oldTag);
            if (i < 0) return;
            const bool collide = (oldTag != newTag) && m_activeTagSet.contains(newTag);
            if (collide) {
                m_activeTags.removeAt(i);
                m_activeTagSet.remove(oldTag);
                // Drop the orphan weight; colliding tag keeps its own value.
                m_tagWeights.remove(oldTag);
            }
            else {
                m_activeTags[i] = newTag;
                m_activeTagSet.remove(oldTag);
                m_activeTagSet.insert(newTag);
                // Migrate weight so the rename doesn't silently drop it.
                if (m_tagWeights.contains(oldTag)) m_tagWeights[newTag] = m_tagWeights.take(oldTag);
            }
            renamePushTag(oldTag, collide ? QString() : newTag);
            tagEdit->setProperty("_tag", newTag);
            captureUndoSnapshot(QStringLiteral("rename:") + oldTag);
            queueRepush();
        });
    }

    rl->addWidget(tagEdit, 1);

    if (hasVar) {
        static const QRegularExpression varRe(R"(\$([A-Za-z0-9_]+)\$)");
        QStringList varNames;
        auto it = varRe.globalMatch(pt.sourceTag);
        while (it.hasNext())
            varNames << "$" + it.next().captured(1) + "$";
        auto* badge = new QLabel(varNames.join(" "));
        badge->setObjectName("ComposerVarBadge");
        badge->setAttribute(Qt::WA_StyledBackground, true);
        rl->addWidget(badge);
    }
    if (pt.result == RuleResult::NoFacets ||
        (pt.result == RuleResult::Injected && pt.facets.isEmpty())) {
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

    // Weight spinbox - shown for tags that appear in the output (not deactivated/removed).
    if (pt.result != RuleResult::Skipped && pt.result != RuleResult::Replaced &&
        pt.result != RuleResult::Deactivated) {
        auto* wSpin = new QDoubleSpinBox;
        wSpin->setObjectName("ComposerWeightSpin");
        wSpin->setRange(0.10, 5.00);
        wSpin->setSingleStep(0.05);
        wSpin->setDecimals(2);
        wSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        wSpin->setAlignment(Qt::AlignCenter);
        wSpin->setFixedWidth(52);
        wSpin->setValue(double(pt.weight));

        const QString weightKey = weightKeyOf(pt);

        auto applyWeightColor = [wSpin](double val) {
            const bool weighted = qAbs(val - 1.0) > 0.001;
            wSpin->setProperty("weighted", weighted);
            wSpin->style()->unpolish(wSpin);
            wSpin->style()->polish(wSpin);
            wSpin->update();
        };
        applyWeightColor(double(pt.weight));

        connect(wSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this, weightKey, applyWeightColor](double val) {
                    const float w = float(val);
                    m_tagWeights[weightKey] = w;
                    for (auto& p : m_lastResult)
                        if (weightKeyOf(p) == weightKey) p.weight = w;
                    applyWeightColor(val);
                    captureUndoSnapshot(QStringLiteral("weight:") + weightKey);
                });
        rl->addWidget(wSpin);
    }

    const QString wikiTag = pt.tag;
    if (inActive) {
        auto onRemove = [this, activeKey]() {
            m_activeTags.removeOne(activeKey);
            m_activeTagSet.remove(activeKey);
            // weightKeyOf == sourceTag == activeKey for $VAR$ tags; using
            // wikiTag (expanded form) would orphan the entry.
            m_tagWeights.remove(activeKey);
            m_deactivatedTags.remove(activeKey);
            m_deactivatedCategory.remove(activeKey);
            captureUndoSnapshot();
            queueRepush();
        };

        // Snapshot the row's current category so a later display rebuild
        // can keep the deactivated tag in this section.
        const QString originalCategory = m_groups.groupFor(pt.facets);
        auto onToggleDeactivate = [this, activeKey, originalCategory]() {
            if (m_deactivatedTags.contains(activeKey)) {
                m_deactivatedTags.remove(activeKey);
                m_deactivatedCategory.remove(activeKey);
            }
            else {
                m_deactivatedTags.insert(activeKey);
                m_deactivatedCategory.insert(activeKey, originalCategory);
            }
            captureUndoSnapshot();
            queueRepush();
        };

        auto* delBtn = new QPushButton("✕", row);
        delBtn->setObjectName("TagRemoveBtn");
        delBtn->setFixedSize(18, 18);
        delBtn->setCursor(Qt::PointingHandCursor);
        connect(delBtn, &QPushButton::clicked, this, onRemove);
        rl->addWidget(delBtn, 0, Qt::AlignVCenter);

        auto installMenu = [&](QWidget* w) {
            w->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(w, &QWidget::customContextMenuRequested, this,
                    [this, wikiTag, activeKey, hasVar, onRemove, onToggleDeactivate,
                     isDeactivated](const QPoint&) {
                        QMenu menu;
                        QAction* wikiAct = menu.addAction("Wiki");
                        QAction* facetAct = menu.addAction("Edit facets");
                        QAction* deactAct =
                            menu.addAction(isDeactivated ? "Activate" : "Deactivate");
                        QAction* removeAct = menu.addAction("Remove");

                        // Variable swap: retargets every $foo$ or strips them.
                        QAction* dropVarAct = nullptr;
                        QHash<QAction*, QString> setVarActs;
                        if (hasVar && m_varIndex) {
                            QMenu* varMenu = menu.addMenu("Change variable");
                            dropVarAct = varMenu->addAction("Remove variable");
                            if (!m_varIndex->variables().isEmpty()) varMenu->addSeparator();
                            for (const auto& v : m_varIndex->variables()) {
                                QAction* a = varMenu->addAction("$" + v.name + "$");
                                setVarActs.insert(a, v.name);
                            }
                        }

                        const QHash<QAction*, QString> quickFacetActs = addQuickFacetActions(menu);

                        QAction* chosen = menu.exec(QCursor::pos());
                        if (chosen == wikiAct)
                            emit wikiRequested(wikiTag);
                        else if (chosen == facetAct)
                            emit facetEditorRequested(wikiTag);
                        else if (chosen == deactAct)
                            onToggleDeactivate();
                        else if (chosen == removeAct)
                            onRemove();
                        else if (chosen && quickFacetActs.contains(chosen))
                            emit quickFacetRequested(wikiTag, quickFacetActs.value(chosen));
                        else if (dropVarAct && chosen == dropVarAct)
                            replaceTagVariable(activeKey, QString());
                        else if (chosen && setVarActs.contains(chosen))
                            replaceTagVariable(activeKey, setVarActs.value(chosen));
                    });
        };
        installMenu(row);
        installMenu(tagEdit);
    }
    else {
        auto installWiki = [&](QWidget* w) {
            w->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(w, &QWidget::customContextMenuRequested, this, [this, wikiTag](const QPoint&) {
                QMenu menu;
                QAction* wikiAct = menu.addAction("Wiki");
                QAction* facetAct = menu.addAction("Edit facets");
                const QHash<QAction*, QString> quickFacetActs = addQuickFacetActions(menu);
                QAction* chosen = menu.exec(QCursor::pos());
                if (chosen == wikiAct)
                    emit wikiRequested(wikiTag);
                else if (chosen == facetAct)
                    emit facetEditorRequested(wikiTag);
                else if (chosen && quickFacetActs.contains(chosen))
                    emit quickFacetRequested(wikiTag, quickFacetActs.value(chosen));
            });
        };
        installWiki(row);
        installWiki(tagEdit);
    }

    return row;
}

// ---- Rules reload

void PromptComposerPage::setEntryModel(core::EntryModel* model)
{
    m_entryModel = model;
}

void PromptComposerPage::setQuickFacets(const QString& characterFacet,
                                        const QString& copyrightFacet,
                                        const QString& triggerWordFacet, const QString& styleFacet)
{
    m_quickCharFacet = characterFacet;
    m_quickCopyFacet = copyrightFacet;
    m_quickTriggerFacet = triggerWordFacet;
    m_quickStyleFacet = styleFacet;
}

void PromptComposerPage::setActiveLoraUuids(const QList<QString>& uuids)
{
    m_activeLoraUuids = uuids;
}

} // namespace gui
