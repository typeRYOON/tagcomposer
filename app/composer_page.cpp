// Construction, floats and the run path. See also composer_page_tags.cpp,
// composer_page_sidebar.cpp and composer_page_states.cpp.

#include <app/composer_page.h>
#include <app/app_data.h>
#include <app/app_scroll_bar.h>
#include <app/category_nav_panel.h>
#include <app/comfy_client.h>
#include <app/composer_scroll_area.h>
#include <app/composer_widgets.h>
#include <app/icons.h>
#include <app/paths.h>
#include <app/preview_popout_window.h>
#include <app/states_grid_view.h>
#include <app/tag_preview_popup.h>
#include <app/tag_search_bar.h>
#include <app/widget_utils.h>
#include <app/workflow_input_cache.h>
#include <core/entry_store.h>
#include <core/prompt.h>
#include <core/prompt_history.h>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSet>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedLayout>
#include <QStackedWidget>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

// True when only the weights differ. Compares whole docs so new fields count.
bool weightsOnlyChange(const ComposerDoc& from, const ComposerDoc& to)
{
    if (from.weights == to.weights) return false;

    ComposerDoc probe = to;
    probe.weights = from.weights;
    return probe == from;
}

QString nameOne(const QString& verb, const QStringList& items)
{
    if (items.size() == 1) return u"%1 \"%2\""_s.arg(verb, items.first());
    return u"%1 %2 tags"_s.arg(verb).arg(items.size());
}

// Describes what changed, for undo/redo messages. First match wins.
QString describeChange(const ComposerDoc& from, const ComposerDoc& to)
{
    const QSet<QString> fromTags(from.activeTags.cbegin(), from.activeTags.cend());
    const QSet<QString> toTags(to.activeTags.cbegin(), to.activeTags.cend());

    const QStringList added = QStringList(QSet(toTags - fromTags).values());
    const QStringList removed = QStringList(QSet(fromTags - toTags).values());
    if (!added.isEmpty() && removed.isEmpty()) return nameOne(u"added"_s, added);
    if (!removed.isEmpty() && added.isEmpty()) return nameOne(u"removed"_s, removed);
    if (!added.isEmpty())
        return u"replaced %1 tag(s)"_s.arg(std::max(added.size(), removed.size()));

    const QStringList deactivated = QStringList(QSet(to.deactivated - from.deactivated).values());
    const QStringList reactivated = QStringList(QSet(from.deactivated - to.deactivated).values());
    if (!deactivated.isEmpty()) return nameOne(u"deactivated"_s, deactivated);
    if (!reactivated.isEmpty()) return nameOne(u"reactivated"_s, reactivated);

    {
        QSet<QString> keys(from.weights.keyBegin(), from.weights.keyEnd());
        for (auto it = to.weights.keyBegin(); it != to.weights.keyEnd(); ++it) keys.insert(*it);

        QStringList changed;
        for (const QString& key : keys)
            if (!qFuzzyCompare(from.weights.value(key, 1.0f), to.weights.value(key, 1.0f)))
                changed << key;

        if (changed.size() == 1)
            return u"weight on \"%1\" (%2)"_s.arg(
                changed.first(),
                QString::number(double(to.weights.value(changed.first(), 1.0f)), 'f', 2));
        if (!changed.isEmpty()) return u"%1 weight changes"_s.arg(changed.size());
    }

    if (from.customFacets != to.customFacets) return u"tag facets"_s;
    if (from.loraStack != to.loraStack) return u"LoRA stack"_s;
    if (from.pushes.size() != to.pushes.size())
        return to.pushes.size() > from.pushes.size() ? u"entry push"_s : u"entry un-push"_s;

    return u"changes"_s;
}

// Resolves one {date format} in a path against today.
QString resolveDatePattern(const QString& pattern)
{
    if (pattern.isEmpty()) return {};

    const qsizetype open = pattern.indexOf(u'{');
    if (open < 0) return pattern;

    const qsizetype close = pattern.indexOf(u'}', open);
    if (close < 0) return pattern.first(open);

    return pattern.first(open)
        + QDateTime::currentDateTime().toString(pattern.sliced(open + 1, close - open - 1))
        + pattern.sliced(close + 1);
}

QPushButton* sidebarButton(const QIcon& icon, const QString& tooltip)
{
    auto* button = new QPushButton;
    button->setObjectName(u"SidebarBtn"_s);
    button->setFixedSize(20, 20);
    button->setIcon(icon);
    button->setIconSize(QSize(14, 14));
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(tooltip);
    return button;
}

QWidget* sectionHeader(const QString& title, const QList<QWidget*>& trailing,
                       const QMargins& margins = QMargins(12, 10, 10, 6))
{
    auto* header = new QWidget;
    header->setObjectName(u"ComposerHeaderRow"_s);
    header->setAttribute(Qt::WA_StyledBackground, true);

    auto* layout = new QHBoxLayout(header);
    layout->setContentsMargins(margins);
    layout->setSpacing(4);

    auto* label = new QLabel(title);
    label->setObjectName(u"ComposerHeaderLabel"_s);
    layout->addWidget(label, 1);

    for (QWidget* widget : trailing)
        layout->addWidget(widget);
    return header;
}

QWidget* stackSection(QWidget* header, QWidget* body)
{
    auto* section = new QWidget;
    auto* layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
    layout->addWidget(body, 1);
    return section;
}

} // namespace

ComposerPage::ComposerPage(ComposerStore& store, AppData& data, EntryStore& entries,
                           WorkflowInputCache& cache, ComfyClient& comfy,
                           PromptHistory& history, QWidget* parent)
    : QWidget(parent), m_store(&store), m_data(&data), m_entries(&entries), m_cache(&cache),
      m_comfy(&comfy), m_history(&history)
{
    setObjectName(u"PromptComposerPage"_s);
    setAttribute(Qt::WA_StyledBackground, true);

    // So Escape from the search bar has somewhere to land.
    setFocusPolicy(Qt::StrongFocus);

    // Dropping a PNG with a baked state restores it.
    setAcceptDrops(true);

    QWidget* centre = buildCentre();
    m_sidebar = buildSidebar();

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(centre, 1);
    root->addWidget(m_sidebar);

    buildFloats();

    connect(m_store, &ComposerStore::docChanged, this, &ComposerPage::refresh);
    connect(m_store, &ComposerStore::undoStateChanged, this, [this](bool undo, bool redo) {
        if (m_undoBtn) m_undoBtn->setEnabled(undo);
        if (m_redoBtn) m_redoBtn->setEnabled(redo);
    });

    connect(m_comfy, &ComfyClient::failed, this,
            [this](const QString& reason) { emit statusMessage(reason); });
    connect(m_comfy, &ComfyClient::acknowledged, this,
            [this](const QString& what) { emit statusMessage(what + u" sent"_s); });

    // The rest waits for reloadAll().
    rebuildWorkflowList();
    rebuildStatesList();
    refresh();
}

void ComposerPage::reloadAll()
{
    applySettings();
    reloadProfiles();
    rebuildRulesSidebar();
    rebuildVarsSidebar();
    rebuildWorkflowList();
    rebuildStatesList();

    // Doesn't create the folder; saving does.
    m_statesDir = m_data->dataPath(paths::kStatesDir);
    m_stateManager = StateManager::loadFromDir(m_statesDir);
    if (m_statesGrid) m_statesGrid->setStates(&m_stateManager.states());
    rebuildStatesList();

    refresh();
}

QWidget* ComposerPage::buildCentre()
{
    m_searchBar = new TagSearchBar(this);
    m_searchBar->setActiveTags(&m_activeTagSet);

    m_filterDebounce = new QTimer(this);
    m_filterDebounce->setSingleShot(true);
    m_filterDebounce->setInterval(180);
    connect(m_filterDebounce, &QTimer::timeout, this, [this]() {
        m_freezeNextRebuild = true; // fade the rebuild
        applyTagFilter();
    });

    connect(m_searchBar, &TagSearchBar::queryChanged, this, [this](const QString& text) {
        m_filterQuery = text.trimmed();
        m_filterDebounce->start();
    });

    connect(m_searchBar, &TagSearchBar::tagAdded, this, [this](const QString& tag) {
        // A paste may arrive comma-joined.
        QStringList parts;
        if (tag.contains(u','))
            parts = tag.split(u',', Qt::SkipEmptyParts);
        else
            parts << tag;

        QStringList clean;
        for (const QString& raw : parts) {
            const QString trimmed = raw.trimmed();
            if (!trimmed.isEmpty()) clean << trimmed;
        }
        if (clean.isEmpty()) return;

        m_freezeNextRebuild = true;
        m_store->addTags(clean);
    });

    // Re-entering a deactivated tag reactivates it.
    connect(m_searchBar, &TagSearchBar::tagAlreadyPresent, this, [this](const QString& tag) {
        if (!m_store->doc().deactivated.contains(tag)) return;
        m_freezeNextRebuild = true;
        m_store->setDeactivated(tag, false);
        emit statusMessage(u"Reactivated: %1"_s.arg(tag));
    });

    connect(m_searchBar, &TagSearchBar::downArrowOnEmpty, this, [this]() {
        if (m_tagRowWidgets.isEmpty() || !m_groupsScroll) return;
        m_groupsScroll->setFocus(Qt::OtherFocusReason);
        setSelectedRow(m_selectedRowIndex < 0
                           ? 0
                           : std::min(m_selectedRowIndex + 1, int(m_tagRowWidgets.size()) - 1));
    });

    connect(m_searchBar, &TagSearchBar::escapePressed, this, [this]() {
        // Prefer the list so arrow keys work; the page takes focus when it's hidden.
        QWidget* target = (m_groupsScroll && m_groupsScroll->isVisible())
            ? static_cast<QWidget*>(m_groupsScroll)
            : static_cast<QWidget*>(this);
        target->setFocus(Qt::OtherFocusReason);
    });

    // ---- The tag list
    m_groupsContainer = new QWidget;
    m_groupsContainer->setObjectName(u"ComposerGroupsContainer"_s);
    m_groupsContainer->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_groupsContainer, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
                QMenu menu;
                addCustomTagMenu(menu);
                menu.exec(m_groupsContainer->mapToGlobal(pos));
            });

    m_groupsLayout = new QVBoxLayout(m_groupsContainer);
    m_groupsLayout->setContentsMargins(12, 12, 12, 12);
    m_groupsLayout->setSpacing(2);
    m_groupsLayout->addStretch();

    m_groupsScroll = new ComposerScrollArea;
    m_groupsScroll->setObjectName(u"ComposerScroll"_s);
    m_groupsScroll->setWidget(m_groupsContainer);
    m_groupsScroll->setWidgetResizable(true);
    m_groupsScroll->setFrameShape(QFrame::NoFrame);
    m_groupsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_groupsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_groupsScroll->installEventFilter(this);

    connect(m_groupsScroll, &ComposerScrollArea::runRequested, this, &ComposerPage::triggerRun);
    connect(m_groupsScroll, &ComposerScrollArea::interruptRequested, m_comfy,
            &ComfyClient::interrupt);
    connect(m_groupsScroll, &ComposerScrollArea::clearPendingRequested, m_comfy,
            &ComfyClient::clearPending);

    // Tab from the list jumps to the search bar.
    auto* tabToSearch = new QShortcut(QKeySequence(Qt::Key_Tab), m_groupsScroll);
    tabToSearch->setContext(Qt::WidgetWithChildrenShortcut);
    connect(tabToSearch, &QShortcut::activated, this,
            [this]() { m_searchBar->setFocus(Qt::TabFocusReason); });

    auto* emptyHint = new QLabel(
        u"Press \"Composer Toggle\" on an entry image\nto push its tags here."_s);
    emptyHint->setObjectName(u"ComposerEmptyHint"_s);
    emptyHint->setAlignment(Qt::AlignCenter);

    m_mainStack = new QStackedWidget;
    m_mainStack->addWidget(emptyHint);      // 0
    m_mainStack->addWidget(m_groupsScroll); // 1

    m_emptyHintFx = new QGraphicsOpacityEffect(emptyHint);
    m_emptyHintFx->setOpacity(1.0);
    emptyHint->setGraphicsEffect(m_emptyHintFx);

    m_groupsFx = new QGraphicsOpacityEffect(m_groupsScroll);
    m_groupsFx->setOpacity(1.0);
    m_groupsScroll->setGraphicsEffect(m_groupsFx);

    m_mainStackFade = new QPropertyAnimation(this);
    m_mainStackFade->setPropertyName("opacity");
    m_mainStackFade->setDuration(180);
    m_mainStackFade->setEasingCurve(QEasingCurve::InOutSine);

    // ---- States grid (replaces the tag list when toggled)
    m_statesGrid = new StatesGridView;
    m_statesGrid->setObjectName(u"StatesGrid"_s);
    m_statesGrid->setStates(&m_stateManager.states());

    connect(m_statesGrid, &StatesGridView::tileClicked, this, [this](int row) {
        if (row < 0 || row >= m_stateManager.states().size()) return;
        m_freezeNextRebuild = true;
        setStatesViewActive(false);
        restoreState(m_stateManager.states()[row]);
    });

    connect(m_statesGrid, &StatesGridView::tileImageDropped, this,
            [this](int row, const QString& sourcePath) {
                if (row < 0 || row >= m_stateManager.states().size()) return;

                SavedState& state = m_stateManager.states()[row];
                const QString stateDir = m_statesDir + u"/"_s + state.id;
                QDir().mkpath(stateDir);

                if (!state.previewImagePath.isEmpty()
                    && QFile::exists(state.previewImagePath))
                    QFile::remove(state.previewImagePath);

                // Cap the stored preview size.
                QImage image(sourcePath);
                const QString destination = stateDir + u"/preview.png"_s;
                if (image.isNull()) {
                    if (!QFile::copy(sourcePath, destination)) return;
                } else {
                    constexpr int maxWidth = 1024;
                    constexpr int maxHeight = 1280;
                    if (image.width() > maxWidth || image.height() > maxHeight)
                        image = image.scaled(maxWidth, maxHeight, Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation);
                    if (!image.save(destination, "PNG")) return;
                }

                const QString id = state.id;
                state.previewImagePath = destination;
                state.createdAt = QDateTime::currentMSecsSinceEpoch();
                if (row != 0) m_stateManager.states().move(row, 0);

                m_stateManager.saveToDir(m_statesDir);
                m_statesGrid->invalidateTile(id);
                rebuildStatesList();
            });

    connect(m_statesGrid, &StatesGridView::tileContextMenuRequested, this,
            [this](int row, QPoint globalPos) {
                if (row < 0 || row >= m_stateManager.states().size()) return;

                QMenu menu;
                QAction* openAction = menu.addAction(u"Open state file"_s);
                menu.addSeparator();
                QAction* overwriteAction = menu.addAction(u"Overwrite with current"_s);
                QAction* renameAction = menu.addAction(u"Rename"_s);
                QAction* deleteAction = menu.addAction(u"Delete"_s);

                QAction* chosen = menu.exec(globalPos);
                if (!chosen) return;

                if (chosen == openAction) {
                    openSystemFile(m_statesDir + u"/"_s + m_stateManager.states()[row].id
                                   + u"/state.json"_s);
                } else if (chosen == overwriteAction) {
                    overwriteState(row);
                } else if (chosen == renameAction) {
                    bool ok = false;
                    const QString name = QInputDialog::getText(
                        this, u"Rename State"_s, u"Name:"_s, QLineEdit::Normal,
                        m_stateManager.states()[row].name, &ok);
                    if (!ok || name.trimmed().isEmpty()) return;

                    const QString id = m_stateManager.states()[row].id;
                    m_stateManager.states()[row].name = name.trimmed();
                    m_stateManager.states()[row].createdAt =
                        QDateTime::currentMSecsSinceEpoch();
                    if (row != 0) m_stateManager.states().move(row, 0);

                    m_stateManager.saveToDir(m_statesDir);
                    m_statesGrid->invalidateTile(id);
                    rebuildStatesList();
                } else if (chosen == deleteAction) {
                    const QString id = m_stateManager.states()[row].id;
                    QDir(m_statesDir + u"/"_s + id).removeRecursively();
                    m_stateManager.states().removeAt(row);
                    m_stateManager.saveToDir(m_statesDir);
                    m_statesGrid->invalidateTile(id);
                    rebuildStatesList();
                }
            });

    m_statesView = new QWidget;
    m_statesViewFx = new QGraphicsOpacityEffect(m_statesView);
    m_statesViewFx->setOpacity(1.0);
    m_statesView->setGraphicsEffect(m_statesViewFx);

    auto* statesLayout = new QVBoxLayout(m_statesView);
    statesLayout->setContentsMargins(0, 0, 0, 0);
    statesLayout->setSpacing(0);

    m_statesFilter = new QLineEdit;
    m_statesFilter->setObjectName(u"TagSearchInput"_s);
    m_statesFilter->setPlaceholderText(u"Filter states..."_s);
    m_statesFilter->setClearButtonEnabled(true);
    statesLayout->addWidget(m_statesFilter);
    statesLayout->addWidget(m_statesGrid, 1);

    m_statesEmptyHint = new QLabel(u"No saved states"_s);
    m_statesEmptyHint->setObjectName(u"ComposerEmptyHint"_s);
    m_statesEmptyHint->setAlignment(Qt::AlignCenter);
    m_statesEmptyHint->hide();
    statesLayout->addWidget(m_statesEmptyHint, 1);

    // Debounced so typing doesn't re-bake tiles per keystroke.
    auto* statesFilterDebounce = new QTimer(this);
    statesFilterDebounce->setSingleShot(true);
    statesFilterDebounce->setInterval(180);
    connect(statesFilterDebounce, &QTimer::timeout, this, [this]() { rebuildStatesList(); });
    connect(m_statesFilter, &QLineEdit::textChanged, statesFilterDebounce,
            qOverload<>(&QTimer::start));

    m_mainStack->addWidget(m_statesView); // 2

    auto* content = new QWidget;
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(m_searchBar);
    contentLayout->addWidget(m_mainStack, 1);

    // Background in its own stacked layer, behind the content.
    auto* background = new QWidget;
    background->setObjectName(u"ComposerCenterBg"_s);

    auto* centre = new QWidget;
    auto* centreStack = new QStackedLayout(centre);
    centreStack->setStackingMode(QStackedLayout::StackAll);
    centreStack->addWidget(background);
    centreStack->addWidget(content);
    centreStack->setCurrentIndex(1);
    return centre;
}

QWidget* ComposerPage::buildSidebar()
{
    // ---- RULES
    m_rulesContainer = new QWidget;
    m_rulesContainer->setObjectName(u"ComposerRulesContainer"_s);
    m_rulesLayout = new QVBoxLayout(m_rulesContainer);
    m_rulesLayout->setContentsMargins(8, 8, 8, 8);
    m_rulesLayout->setSpacing(4);
    m_rulesLayout->addStretch();

    m_rulesContainer->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_rulesContainer, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
                QMenu menu;
                menu.addAction(u"Add rule..."_s, this, [this]() { promptAddRule(); });
                menu.exec(m_rulesContainer->mapToGlobal(pos));
            });

    auto* rulesScroll = new QScrollArea;
    rulesScroll->setObjectName(u"ComposerRulesScroll"_s);
    rulesScroll->setWidget(m_rulesContainer);
    rulesScroll->setWidgetResizable(true);
    rulesScroll->setFrameShape(QFrame::NoFrame);
    rulesScroll->setVerticalScrollBar(new AppScrollBar(Qt::Vertical));
    rulesScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rulesScroll->installEventFilter(this);

    QPushButton* rulesOpen = sidebarButton(icons::openExternal(), u"Open rules.fct in editor"_s);
    connect(rulesOpen, &QPushButton::clicked, this,
            [this]() { openSystemFile(m_data->rulesPath()); });

    QPushButton* rulesReload = sidebarButton(icons::reload(), u"Reload rules from file"_s);
    connect(rulesReload, &QPushButton::clicked, this, &ComposerPage::reloadRules);

    QWidget* rulesSection =
        stackSection(sectionHeader(u"RULES"_s, {rulesOpen, rulesReload}), rulesScroll);

    // ---- WORKFLOWS, with the STATES view toggle in its header
    m_statesToggleBtn = new QPushButton(u"STATES"_s);
    m_statesToggleBtn->setObjectName(u"ComposerModeTab"_s);
    m_statesToggleBtn->setCheckable(true);
    m_statesToggleBtn->setCursor(Qt::PointingHandCursor);
    m_statesToggleBtn->setToolTip(u"Browse saved states"_s);
    connect(m_statesToggleBtn, &QPushButton::toggled, this,
            [this](bool on) { setStatesViewActive(on); });

    QPushButton* workflowEditBtn =
        sidebarButton(icons::openExternal(), u"Workflow Variable Editor"_s);
    connect(workflowEditBtn, &QPushButton::clicked, this,
            &ComposerPage::workflowEditorRequested);

    m_saveStateBtn = sidebarButton(icons::plus(), u"Save current state"_s);
    connect(m_saveStateBtn, &QPushButton::clicked, this, &ComposerPage::saveCurrentState);

    auto* workflowHeader = new QWidget;
    workflowHeader->setObjectName(u"ComposerHeaderRow"_s);
    workflowHeader->setAttribute(Qt::WA_StyledBackground, true);

    auto* workflowHeaderLayout = new QHBoxLayout(workflowHeader);
    workflowHeaderLayout->setContentsMargins(8, 6, 8, 4);
    workflowHeaderLayout->setSpacing(2);

    auto* workflowLabel = new QLabel(u"WORKFLOWS"_s);
    workflowLabel->setObjectName(u"ComposerHeaderLabel"_s);
    workflowHeaderLayout->addWidget(workflowLabel);
    workflowHeaderLayout->addSpacing(8);
    workflowHeaderLayout->addWidget(m_statesToggleBtn);
    workflowHeaderLayout->addStretch(1);
    workflowHeaderLayout->addWidget(workflowEditBtn);
    workflowHeaderLayout->addWidget(m_saveStateBtn);

    m_workflowList = new WorkflowDropList(this);
    m_workflowList->setObjectName(u"WfList"_s);
    m_workflowList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_workflowList->setVerticalScrollBar(new AppScrollBar(Qt::Vertical));
    m_workflowList->installEventFilter(this);

    m_workflowFilter = new QLineEdit;
    m_workflowFilter->setObjectName(u"ComposerVarEdit"_s);
    m_workflowFilter->setPlaceholderText(u"Filter workflows..."_s);
    m_workflowFilter->setClearButtonEnabled(true);
    connect(m_workflowFilter, &QLineEdit::textChanged, this,
            [this](const QString&) { rebuildWorkflowList(); });

    auto* workflowBody = new QWidget;
    auto* workflowBodyLayout = new QVBoxLayout(workflowBody);
    workflowBodyLayout->setContentsMargins(0, 0, 0, 0);
    workflowBodyLayout->setSpacing(2);
    workflowBodyLayout->addWidget(m_workflowFilter);
    workflowBodyLayout->addWidget(m_workflowList, 1);
    workflowBody->setMinimumHeight(80);

    QWidget* workflowSection = stackSection(workflowHeader, workflowBody);

    // ---- VARIABLES
    m_varsContainer = new QWidget;
    m_varsContainer->setObjectName(u"ComposerVarsContainer"_s);
    m_varsLayout = new QVBoxLayout(m_varsContainer);
    m_varsLayout->setContentsMargins(8, 6, 8, 8);
    m_varsLayout->setSpacing(6);

    auto* varsScroll = new QScrollArea;
    varsScroll->setObjectName(u"ComposerVarsScroll"_s);
    varsScroll->setWidget(m_varsContainer);
    varsScroll->setWidgetResizable(true);
    varsScroll->setFrameShape(QFrame::NoFrame);
    varsScroll->setVerticalScrollBar(new AppScrollBar(Qt::Vertical));
    varsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    varsScroll->installEventFilter(this);

    QPushButton* varsOpen = sidebarButton(icons::openExternal(), u"Open vars.fct in editor"_s);
    connect(varsOpen, &QPushButton::clicked, this,
            [this]() { openSystemFile(m_data->variablesPath()); });

    QPushButton* varsReload = sidebarButton(icons::reload(), u"Reload variables from file"_s);
    connect(varsReload, &QPushButton::clicked, this, &ComposerPage::reloadVars);

    QWidget* varsSection = stackSection(
        sectionHeader(u"VARIABLES"_s, {varsOpen, varsReload}, QMargins(12, 8, 8, 6)), varsScroll);

    // Rules takes the leftover space.
    auto* split = new QSplitter(Qt::Vertical);
    split->setObjectName(u"ComposerSidebarSplit"_s);
    split->setHandleWidth(5);
    split->setChildrenCollapsible(false);
    split->addWidget(rulesSection);
    split->addWidget(workflowSection);
    split->addWidget(varsSection);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);
    split->setStretchFactor(2, 0);
    split->setSizes({400, 240, 200});

    // ---- PROFILES, pinned above the splitter
    QPushButton* profilesOpen =
        sidebarButton(icons::openExternal(), u"Open profiles.fct in editor"_s);
    connect(profilesOpen, &QPushButton::clicked, this,
            [this]() { openSystemFile(m_data->dataPath(paths::kProfiles)); });

    QPushButton* profilesReload = sidebarButton(icons::reload(), u"Reload profiles from file"_s);
    connect(profilesReload, &QPushButton::clicked, this, &ComposerPage::reloadProfiles);

    m_groupProfileBox = new QComboBox;
    m_groupProfileBox->setObjectName(u"ComposerProfileBox"_s);
    m_groupProfileBox->setToolTip(
        u"Group ordering. Groups match top-to-bottom, so the order decides\n"
        "which group claims a tag as well as where it lands in the prompt."_s);

    m_formatProfileBox = new QComboBox;
    m_formatProfileBox->setObjectName(u"ComposerProfileBox"_s);
    m_formatProfileBox->setToolTip(
        u"Per-facet tag wrapping (the leading @ on artist tags, say).\n"
        "Independent of the ordering profile."_s);

    auto profileRow = [](const QString& text, QComboBox* box) {
        auto* row = new QWidget;
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);

        auto* label = new QLabel(text);
        label->setObjectName(u"ComposerProfileLabel"_s);
        label->setFixedWidth(46);
        layout->addWidget(label);
        layout->addWidget(box, 1);
        return row;
    };

    auto* profilesBody = new QWidget;
    profilesBody->setObjectName(u"ComposerProfilesBody"_s);
    auto* profilesBodyLayout = new QVBoxLayout(profilesBody);
    profilesBodyLayout->setContentsMargins(8, 6, 8, 8);
    profilesBodyLayout->setSpacing(4);
    profilesBodyLayout->addWidget(profileRow(u"Groups"_s, m_groupProfileBox));
    profilesBodyLayout->addWidget(profileRow(u"Format"_s, m_formatProfileBox));

    QWidget* profilesSection =
        stackSection(sectionHeader(u"PROFILES"_s, {profilesOpen, profilesReload}), profilesBody);

    // activated, not currentIndexChanged, so rebuilds don't re-enter. The
    // "(from state)" row has no name and does nothing.
    connect(m_groupProfileBox, &QComboBox::activated, this, [this](int index) {
        const QString name = m_groupProfileBox->itemData(index).toString();
        if (name.isEmpty() || name == m_profiles.activeGroup()) return;
        m_profiles.setActiveGroup(name);
        applyActiveProfiles(true, true);
    });
    connect(m_formatProfileBox, &QComboBox::activated, this, [this](int index) {
        const QString name = m_formatProfileBox->itemData(index).toString();
        if (name.isEmpty() || name == m_profiles.activeFormat()) return;
        m_profiles.setActiveFormat(name);
        applyActiveProfiles(true, true);
    });

    auto* sidebar = new QWidget;
    sidebar->setObjectName(u"ComposerSidebar"_s);
    sidebar->setFixedWidth(330);

    // ClickFocus so blank-space clicks land here; eventFilter sends Tab onward.
    sidebar->setFocusPolicy(Qt::ClickFocus);
    sidebar->installEventFilter(this);

    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);
    sidebarLayout->addWidget(profilesSection);
    sidebarLayout->addWidget(split, 1);
    return sidebar;
}

void ComposerPage::buildFloats()
{
    auto* nav = new CategoryNavPanel(this);
    m_categoryNav = nav;
    nav->onCategoryClicked = [this](const QString& displayName) {
        const auto it = m_groupHeaders.constFind(displayName);
        if (it == m_groupHeaders.cend() || !it.value()) return;
        m_groupsScroll->scrollToY(
            it.value()->mapTo(m_groupsScroll->widget(), QPoint(0, 0)).y());
    };

    m_clearBtn = new QPushButton(u"Clear"_s, this);
    m_clearBtn->setObjectName(u"ComposerClearBtn"_s);
    m_clearBtn->setFixedHeight(26);
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    icons::applyStates(m_clearBtn, icons::close, 11, QColor(0x55, 0x55, 0x55),
                       QColor(0xcc, 0x55, 0x55));
    connect(m_clearBtn, &QPushButton::clicked, this, [this]() {
        for (Rule& rule : m_data->ruleFile.rules)
            rule.enabled = false;
        report(m_data->saveRules());
        rebuildRulesSidebar();

        m_store->clear(); // one undoable edit
        emit pushesChanged();
    });

    m_pushedBtn = new QPushButton(u"Pushed"_s, this);
    m_pushedBtn->setObjectName(u"ComposerPushedBtn"_s);
    m_pushedBtn->setFixedHeight(26);
    m_pushedBtn->setCursor(Qt::PointingHandCursor);
    m_pushedBtn->setIconSize(QSize(11, 11));
    m_pushedBtn->setIcon(icons::pushDown(11, QColor(0x55, 0x55, 0x55)));
    m_pushedBtn->setToolTip(u"Filter to one pushed entry's tags"_s);
    connect(m_pushedBtn, &QPushButton::clicked, this, &ComposerPage::showPushedFilterMenu);

    constexpr int kPreviewSize = 200;
    m_previewLabel = new PreviewClickLabel(this);
    m_previewLabel->setObjectName(u"ComposerPreviewLabel"_s);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setFixedSize(kPreviewSize, kPreviewSize);
    m_previewLabel->hide(); // shown when the first image arrives

    m_controlBar = new QWidget(this);
    m_controlBar->setObjectName(u"ComposerControlBar"_s);
    m_controlBar->setAttribute(Qt::WA_StyledBackground, true);
    m_controlBar->setFixedHeight(33); // 25px buttons plus 4px margins

    m_copyBtn = new QPushButton(u"Copy prompt"_s, m_controlBar);
    m_copyBtn->setObjectName(u"ComposerCopyBtn"_s);
    m_copyBtn->setCursor(Qt::PointingHandCursor);
    connect(m_copyBtn, &QPushButton::clicked, this,
            [this]() { QGuiApplication::clipboard()->setText(currentPromptString(false)); });

    m_undoBtn = new QPushButton(m_controlBar);
    m_undoBtn->setObjectName(u"ComposerUndoBtn"_s);
    m_undoBtn->setFixedSize(25, 25);
    m_undoBtn->setCursor(Qt::PointingHandCursor);
    m_undoBtn->setToolTip(u"Undo (Ctrl+Z)"_s);
    m_undoBtn->setEnabled(false);
    icons::applyStates(m_undoBtn, icons::undo, 13, QColor(0x77, 0x77, 0x77),
                       QColor(0xcc, 0xcc, 0xcc), QColor(0x2a, 0x2a, 0x2a));
    connect(m_undoBtn, &QPushButton::clicked, this, &ComposerPage::undo);

    m_redoBtn = new QPushButton(m_controlBar);
    m_redoBtn->setObjectName(u"ComposerRedoBtn"_s);
    m_redoBtn->setFixedSize(25, 25);
    m_redoBtn->setCursor(Qt::PointingHandCursor);
    m_redoBtn->setToolTip(u"Redo (Ctrl+Shift+Z)"_s);
    m_redoBtn->setEnabled(false);
    icons::applyStates(m_redoBtn, icons::redo, 13, QColor(0x77, 0x77, 0x77),
                       QColor(0xcc, 0xcc, 0xcc), QColor(0x2a, 0x2a, 0x2a));
    connect(m_redoBtn, &QPushButton::clicked, this, &ComposerPage::redo);

    auto* undoShortcut = new QShortcut(QKeySequence::Undo, this);
    undoShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(undoShortcut, &QShortcut::activated, this, &ComposerPage::undo);

    auto* redoShortcut = new QShortcut(QKeySequence(u"Ctrl+Shift+Z"_s), this);
    redoShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(redoShortcut, &QShortcut::activated, this, &ComposerPage::redo);

    m_runBtn = new QPushButton(u"Run"_s, m_controlBar);
    m_runBtn->setObjectName(u"ComposerRunBtn"_s);
    m_runBtn->setIcon(icons::play(14, QColor(0x77, 0xaa, 0xdd)));
    m_runBtn->setIconSize(QSize(12, 12));
    m_runBtn->setCursor(Qt::PointingHandCursor);
    connect(m_runBtn, &QPushButton::clicked, this, &ComposerPage::triggerRun);

    m_promptCountSpin = new QSpinBox(m_controlBar);
    m_promptCountSpin->setObjectName(u"ComposerCountSpin"_s);
    m_promptCountSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_promptCountSpin->setRange(1, 99);
    m_promptCountSpin->setValue(1);
    m_promptCountSpin->setFixedWidth(40);

    m_interruptBtn = new QPushButton(m_controlBar);
    m_interruptBtn->setObjectName(u"ComposerInterruptBtn"_s);
    m_interruptBtn->setFixedSize(25, 25);
    m_interruptBtn->setIcon(icons::stopSquare(14, QColor(0xee, 0x44, 0x44)));
    m_interruptBtn->setIconSize(QSize(11, 11));
    m_interruptBtn->setCursor(Qt::PointingHandCursor);
    m_interruptBtn->setToolTip(u"Interrupt"_s);
    connect(m_interruptBtn, &QPushButton::clicked, m_comfy, &ComfyClient::interrupt);

    m_undefinedToggleBtn = new QPushButton(u"?"_s, m_controlBar);
    m_undefinedToggleBtn->setObjectName(u"ComposerUndefToggle"_s);
    m_undefinedToggleBtn->setCheckable(true);
    m_undefinedToggleBtn->setCursor(Qt::PointingHandCursor);
    m_undefinedToggleBtn->setToolTip(u"Show only tags without facet definitions"_s);
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

    // Starts at zero so the first preview fades in.
    m_previewInsetFx = new QGraphicsOpacityEffect(m_previewLabel);
    m_previewInsetFx->setOpacity(0.0);
    m_previewLabel->setGraphicsEffect(m_previewInsetFx);

    m_previewInsetFade = new QPropertyAnimation(m_previewInsetFx, "opacity", this);
    m_previewInsetFade->setDuration(350);
    m_previewInsetFade->setEasingCurve(QEasingCurve::InOutSine);

    connect(m_previewLabel, &PreviewClickLabel::clicked, this, [this]() {
        if (!m_popout) {
            // No parent: a real top-level window.
            m_popout = new PreviewPopoutWindow(nullptr);
            m_popout->setAttribute(Qt::WA_DeleteOnClose);
            m_popout->installEventFilter(this);

            connect(m_popout, &QObject::destroyed, this, [this]() {
                m_popout = nullptr;
                m_previewLabel->setAttribute(Qt::WA_TransparentForMouseEvents, false);
                // Only fade back if something was showing.
                if (m_previewLabel->isVisible()) fadePreviewInset(1.0);
            });

            m_popout->setActiveCount(m_lastComfyActive);
            if (m_lastComfyStep > 0 && m_lastComfyTotal > 0)
                m_popout->setProgress(m_lastComfyStep, m_lastComfyTotal);

            connect(m_popout, &PreviewPopoutWindow::runRequested, this,
                    &ComposerPage::triggerRun);
            connect(m_popout, &PreviewPopoutWindow::interruptRequested, m_comfy,
                    &ComfyClient::interrupt);
            connect(m_popout, &PreviewPopoutWindow::clearPendingRequested, m_comfy,
                    &ComfyClient::clearPending);
        }

        // Re-resolved per open, in case the date changed.
        if (!m_tempFolder.isEmpty()) m_popout->setTempFolder(m_tempFolder);
        if (!m_outputFolderPattern.isEmpty())
            m_popout->setOutputFolder(resolveDatePattern(m_outputFolderPattern));
        if (!m_currentPixmap.isNull()) m_popout->setImage(m_currentPixmap);

        m_popout->show();
        m_popout->raise();
        m_popout->activateWindow();

        fadePreviewInset(0.0);

        // Let clicks through the invisible inset to the rows underneath.
        m_previewLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    });

    m_composerFloats = {m_previewLabel, m_controlBar, m_categoryNav, m_clearBtn, m_pushedBtn};
}

// ---- Settings and profiles

void ComposerPage::applySettings()
{
    const Settings& settings = m_data->settings;

    m_outputFolderPattern = settings.comfyOutputFolder;
    m_tempFolder = settings.comfyTempFolder;
    if (m_popout) {
        m_popout->setOutputFolder(resolveDatePattern(m_outputFolderPattern));
        m_popout->setTempFolder(m_tempFolder);
    }

    if (m_statesGrid) {
        m_statesGrid->setTileGradient(settings.tileGradientStart, settings.tileGradientAlpha);
        m_statesGrid->setTileTitleColor(QColor(settings.tileTitleColor));
    }
    if (m_searchBar) m_searchBar->setIndex(&m_data->danbooru);

    // settings.json formats apply only without a profile or state stamp.
    if (m_oneOffFormatLabel.isEmpty() && !m_profiles.formatProfile(m_profiles.activeFormat()))
        m_facetFormats = settings.facetFormats;
}

void ComposerPage::reloadProfiles()
{
    const QString path = m_data->dataPath(paths::kProfiles);
    ProfileIndex fresh = ProfileIndex::loadFromFile(path);

    if (fresh.isEmpty()) {
        // First run: seed profiles.fct from the live groups and formats, once groups
        // exist.
        if (m_profiles.isEmpty() && !m_data->groups.all().isEmpty()) {
            m_profiles = ProfileIndex::withDefaults(m_data->groups, m_data->settings.facetFormats);
            m_profiles.saveToFile(path);
        } else if (m_profiles.isEmpty()) {
            m_groups = m_data->groups;
            m_facetFormats = m_data->settings.facetFormats;
            return;
        } else {
            emit statusMessage(
                u"profiles.fct defines no profiles - keeping the loaded set."_s);
            return;
        }
    } else {
        // Keep the live selection if the file still has it.
        const QString group = m_profiles.activeGroup();
        const QString format = m_profiles.activeFormat();
        m_profiles = std::move(fresh);
        if (m_profiles.groupProfile(group)) m_profiles.setActiveGroup(group);
        if (m_profiles.formatProfile(format)) m_profiles.setActiveFormat(format);

        emit statusMessage(u"Profiles reloaded - %1 group, %2 format."_s
                               .arg(m_profiles.groupProfiles().size())
                               .arg(m_profiles.formatProfiles().size()));
    }

    m_oneOffGroupLabel.clear();
    m_oneOffFormatLabel.clear();
    applyActiveProfiles(false, true);
}

void ComposerPage::applyActiveProfiles(bool persist, bool refreshNow)
{
    m_oneOffGroupLabel.clear();
    m_oneOffFormatLabel.clear();

    const bool hasFormat = m_profiles.formatProfile(m_profiles.activeFormat()) != nullptr;
    m_facetFormats = hasFormat ? m_profiles.activeFormats() : m_data->settings.facetFormats;
    m_groups = applyGroupOrder(m_data->groups, m_profiles.activeOrder());

    if (persist) m_profiles.saveActiveToFile(m_data->dataPath(paths::kProfiles));

    rebuildProfilesSidebar();
    if (!refreshNow) return;

    m_freezeNextRebuild = true;
    applyTagFilter();

    QString message = u"Profile: %1 | %2"_s.arg(
        m_profiles.activeGroup().isEmpty() ? u"(none)"_s : m_profiles.activeGroup(),
        hasFormat ? m_profiles.activeFormat() : u"(settings)"_s);

    // Warn when reordering makes a group unreachable.
    const QList<GroupShadow> shadows = shadowedGroups(m_groups);
    if (!shadows.isEmpty()) {
        QStringList named;
        for (qsizetype i = 0; i < shadows.size() && i < 3; ++i)
            named << u"%1 shadows %2"_s.arg(shadows[i].shadower, shadows[i].shadowed);
        message += u"  -  warning: %1"_s.arg(named.join(u"; "_s));
        if (shadows.size() > 3) message += u" (+%1 more)"_s.arg(shadows.size() - 3);
    }
    emit statusMessage(message);
}

// ---- Evaluation

PipelineContext ComposerPage::pipelineContext() const
{
    return {&m_data->defsFile.defs, &m_data->ruleFile.rules, &m_data->varsFile.vars};
}

QList<PipelineTag> ComposerPage::evaluateTags(const QStringList& tags) const
{
    ComposerDoc doc;
    doc.activeTags = tags;
    doc.weights = m_store->doc().weights;
    doc.customFacets = m_store->doc().customFacets;
    return evaluate(doc, pipelineContext());
}

void ComposerPage::undo()
{
    if (!m_store->canUndo()) {
        emit statusMessage(u"Nothing to undo."_s);
        return;
    }

    // Describe the undone action, not its inverse.
    const ComposerDoc before = m_store->doc();
    m_store->undo();
    emit statusMessage(u"Undo: %1"_s.arg(describeChange(m_store->doc(), before)));
}

void ComposerPage::redo()
{
    if (!m_store->canRedo()) {
        emit statusMessage(u"Nothing to redo."_s);
        return;
    }

    const ComposerDoc before = m_store->doc();
    m_store->redo();
    emit statusMessage(u"Redo: %1"_s.arg(describeChange(before, m_store->doc())));
}

void ComposerPage::refresh()
{
    const ComposerDoc& doc = m_store->doc();

    // A weight change from a row's spin box patches the cached result instead of
    // rebuilding, which would destroy the spin box mid-drag.
    if (m_weightFromSpin && weightsOnlyChange(m_lastDoc, doc)) {
        for (PipelineTag& tag : m_lastResult)
            tag.weight = doc.weights.value(documentKey(tag), 1.0f);
        m_lastDoc = doc;
        return;
    }

    // Evaluate now; callers read m_lastResult right away. Only widgets wait.
    m_lastResult = evaluate(doc, pipelineContext());

    m_activeTagSet.clear();
    for (const QString& tag : doc.activeTags)
        m_activeTagSet.insert(tag);

    m_lastDoc = doc;
    queueRebuild();
}

// Rebuilds destroy every row, and this is often reached from a row's own
// handler, so it runs on the next event-loop turn. Bursts collapse into one.
void ComposerPage::queueRebuild()
{
    if (m_rebuildPending) return;
    m_rebuildPending = true;

    QMetaObject::invokeMethod(
        this,
        [this]() {
            m_rebuildPending = false;
            m_freezeNextRebuild = true;
            applyTagFilter();
        },
        Qt::QueuedConnection);
}

QString ComposerPage::currentPromptString(bool forJson) const
{
    return buildPromptString(bucketByGroup(m_lastResult, m_groups), forJson, m_facetFormats);
}

QStringList ComposerPage::currentActiveTags() const
{
    // Custom-faceted tags don't need triage.
    QStringList out;
    const ComposerDoc& doc = m_store->doc();
    for (const QString& tag : doc.activeTags)
        if (!doc.customFacets.contains(tag)) out << tag;

    QSet<QString> seen(out.cbegin(), out.cend());
    for (const PipelineTag& tag : m_lastResult) {
        if (tag.result != TagResult::Injected || seen.contains(tag.tag)) continue;
        seen.insert(tag.tag);
        out << tag.tag;
    }
    return out;
}

QString ComposerPage::computePromptForTags(const QStringList& tags, bool forJson) const
{
    return buildPromptString(bucketByGroup(evaluateTags(tags), m_groups), forJson,
                             m_facetFormats);
}

QString ComposerPage::computePromptWithExtraTags(const QStringList& extraTags, bool forJson) const
{
    // The document first, then the extras, deduplicated but in order.
    QStringList merged;
    QSet<QString> seen;

    const ComposerDoc& doc = m_store->doc();
    for (const QString& tag : doc.activeTags) {
        if (doc.deactivated.contains(tag) || seen.contains(tag)) continue;
        seen.insert(tag);
        merged << tag;
    }
    for (const QString& tag : extraTags) {
        if (seen.contains(tag)) continue;
        seen.insert(tag);
        merged << tag;
    }
    return computePromptForTags(merged, forJson);
}

int ComposerPage::currentPromptCount() const
{
    return m_promptCountSpin ? m_promptCountSpin->value() : 1;
}

// ---- Run

const Workflow* ComposerPage::selectedWorkflow() const
{
    const int index = m_data->workflows.selectedIndex;
    if (index < 0 || index >= m_data->workflows.workflows.size()) return nullptr;
    return &m_data->workflows.workflows[index];
}

void ComposerPage::report(const QString& error)
{
    if (!error.isEmpty()) emit statusMessage(error);
}

void ComposerPage::triggerRun()
{
    for (int i = 0; i < currentPromptCount(); ++i)
        run();
}

void ComposerPage::run()
{
    const Workflow* workflow = selectedWorkflow();
    if (!workflow) {
        emit statusMessage(u"No workflow selected"_s);
        return;
    }

    QFile file(m_data->workflowPath(*workflow));
    if (!file.open(QIODevice::ReadOnly)) {
        emit statusMessage(u"Cannot read %1: %2"_s.arg(workflow->path, file.errorString()));
        return;
    }
    const QString templateJson = QString::fromUtf8(file.readAll());

    const RunIssues issues = validateRun(m_store->doc(), *workflow, templateJson);
    if (issues.blocked()) {
        emit statusMessage(issues.errors.join(u"  |  "_s));
        return;
    }

    RenderContext context;
    context.pipeline = pipelineContext();
    context.groups = &m_groups;
    context.formats = m_facetFormats;
    context.imageSubfolder = WorkflowInputCache::serverSubfolder();

    const RunRequest request = renderRun(m_store->doc(), *workflow, templateJson, context,
                                         QRandomGenerator::global());

    // Baked into the PNG via extra_pnginfo; dropping the image here restores it.
    const SavedState snapshot = currentSnapshot();

    QJsonObject bakedState;
    bakedState[u"tagcomposer_state"_s] = snapshot.toJson();
    m_comfy->queue(request.json, bakedState);

    PromptRecord record;
    record.queuedAt = QDateTime::currentDateTime();
    record.workflowName = workflow->name;
    record.positivePrompt = currentPromptString(false);
    record.renderedJson = request.json;
    record.snapshot = snapshot;
    record.lorasUsed = m_store->doc().loraStack;
    m_history->append(std::move(record));

    // Store advanced seeds only after queueing, so a blocked run doesn't burn one.
    for (const SeedAdvance& advance : request.nextSeeds) {
        for (WorkflowVar& var : m_data->workflows.workflows[m_data->workflows.selectedIndex].vars) {
            if (var.placeholder != advance.placeholder) continue;
            if (auto* seed = std::get_if<SeedVar>(&var.value)) seed->value = advance.value;
        }
    }
    if (!request.nextSeeds.isEmpty()) report(m_data->saveWorkflows());

    if (!issues.warnings.isEmpty()) emit statusMessage(issues.warnings.join(u"  |  "_s));
}

// ---- Preview

void ComposerPage::setPreviewImage(const QImage& image)
{
    if (image.isNull()) return;

    m_currentPixmap = QPixmap::fromImage(image);

    // Fixed size; size() is 0x0 before the first layout.
    m_previewLabel->setPixmap(
        m_currentPixmap.scaled(QSize(200, 200), Qt::KeepAspectRatio, Qt::SmoothTransformation));

    const bool popoutOpen = m_popout && m_popout->isVisible();

    // Only in composer view; the pixmap is kept for when it returns.
    if (!popoutOpen && !m_statesViewActive) {
        m_previewLabel->show();
        m_previewLabel->raise();
        fadePreviewInset(1.0);
    }
    repositionFloats();

    if (popoutOpen) m_popout->setImage(m_currentPixmap);
}

void ComposerPage::fadePreviewInset(qreal target)
{
    if (!m_previewInsetFx || !m_previewInsetFade) return;
    if (qFuzzyCompare(m_previewInsetFx->opacity(), target)) return;

    m_previewInsetFade->stop();
    m_previewInsetFade->setStartValue(m_previewInsetFx->opacity());
    m_previewInsetFade->setEndValue(target);
    m_previewInsetFade->start();
}

void ComposerPage::setComfyProgress(int step, int total)
{
    m_lastComfyStep = step;
    m_lastComfyTotal = total;
    if (m_popout) m_popout->setProgress(step, total);
}

void ComposerPage::setComfyActiveCount(int count)
{
    m_lastComfyActive = count;
    if (m_popout) m_popout->setActiveCount(count);
}

// ---- Layout

void ComposerPage::repositionFloats()
{
    constexpr int sidebarWidth = 220;
    constexpr int marginRight = 160;
    constexpr int marginTop = 42;
    constexpr int marginBottom = 12;
    constexpr int gap = 4;
    constexpr int buttonGap = 2;

    const int right = width() - sidebarWidth - marginRight;
    int bottom = height() - marginBottom;

    if (m_categoryNav) {
        m_categoryNav->move(right - m_categoryNav->width(), marginTop);
        m_categoryNav->raise();
    }
    if (m_clearBtn) {
        const int navLeft = m_categoryNav ? m_categoryNav->x() : right;
        m_clearBtn->move(navLeft - m_clearBtn->width() - buttonGap, marginTop);
        m_clearBtn->raise();
    }
    if (m_pushedBtn) {
        const int clearLeft = m_clearBtn ? m_clearBtn->x() : right;
        m_pushedBtn->move(clearLeft - m_pushedBtn->width() - buttonGap, marginTop);
        m_pushedBtn->raise();
    }

    m_controlBar->move(right - m_controlBar->width(), bottom - m_controlBar->height());
    bottom -= m_controlBar->height() + gap;

    // Positioned even while hidden so it reappears in the right place.
    m_previewLabel->move(right - m_previewLabel->width(),
                         bottom - m_previewLabel->height());

    // Pad the list so its last rows can scroll clear of the floats.
    if (!m_groupsLayout || !m_groupsScroll) return;

    const QWidget* topFloat = nullptr;
    if (m_previewLabel->isVisible())
        topFloat = m_previewLabel;
    else if (m_controlBar->isVisible())
        topFloat = m_controlBar;

    int pad = 0;
    if (topFloat) {
        QWidget* viewport = m_groupsScroll->viewport();
        const int viewportBottom = viewport->mapTo(this, QPoint(0, viewport->height())).y();
        pad = qMax(0, viewportBottom - topFloat->y()) + gap;
    }

    const QMargins margins = m_groupsLayout->contentsMargins();
    if (margins.bottom() != margins.top() + pad)
        m_groupsLayout->setContentsMargins(margins.left(), margins.top(), margins.right(),
                                           margins.top() + pad);
}

void ComposerPage::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    repositionFloats();
}

void ComposerPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    repositionFloats();
    // The workflow editor may have changed the list.
    rebuildWorkflowList();
}

void ComposerPage::reloadWorkflows()
{
    rebuildWorkflowList();
}

int ComposerPage::composerStackIndex() const
{
    return m_lastResult.isEmpty() ? 0 : 1;
}

QGraphicsOpacityEffect* ComposerPage::stackChildFx(int index) const
{
    switch (index) {
    case 0:
        return m_emptyHintFx;
    case 1:
        return m_groupsFx;
    case 2:
        return m_statesViewFx;
    default:
        return nullptr;
    }
}

TagPreviewPopup* ComposerPage::tagPreviewPopup()
{
    if (!m_tagPreviewPopup) m_tagPreviewPopup = new TagPreviewPopup(this);
    return m_tagPreviewPopup;
}

bool ComposerPage::eventFilter(QObject* watched, QEvent* event)
{
    // Tab from the sidebar goes to the search bar (or the states filter).
    if (event->type() == QEvent::KeyPress && m_sidebar) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Tab && key->modifiers() == Qt::NoModifier) {
            auto* widget = qobject_cast<QWidget*>(watched);
            if (widget && (widget == m_sidebar || m_sidebar->isAncestorOf(widget))) {
                if (m_statesViewActive && m_statesFilter)
                    m_statesFilter->setFocus(Qt::TabFocusReason);
                else if (m_searchBar)
                    m_searchBar->setFocus(Qt::TabFocusReason);
                return true;
            }
        }
    }

    if (watched == m_groupsScroll && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);

        if (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up) {
            if (m_tagRowWidgets.isEmpty()) return false;

            int next = m_selectedRowIndex;
            if (next < 0)
                next = key->key() == Qt::Key_Down ? 0 : int(m_tagRowWidgets.size()) - 1;
            else
                next += key->key() == Qt::Key_Down ? 1 : -1;

            setSelectedRow(std::clamp(next, 0, int(m_tagRowWidgets.size()) - 1));
            return true;
        }

        const Qt::KeyboardModifiers modifiers = key->modifiers() & ~Qt::ShiftModifier;
        const bool haveRow =
            m_selectedRowIndex >= 0 && m_selectedRowIndex < m_tagRowWidgets.size();

        // Delete removes the selected row.
        if (modifiers == Qt::NoModifier && key->key() == Qt::Key_Delete && haveRow) {
            QLineEdit* edit = m_tagRowWidgets[m_selectedRowIndex]->findChild<QLineEdit*>();
            const QString activeKey = edit ? edit->property("_tag").toString() : QString();
            if (!activeKey.isEmpty() && m_activeTagSet.contains(activeKey)) {
                m_store->removeTag(activeKey);
                return true;
            }
        }

        // Type to edit: printable keys and Backspace go to the selected row's edit.
        // Not with modifiers, so shortcuts still work.
        const bool printable = !key->text().isEmpty() && key->text()[0].isPrint();
        const bool editing = key->key() == Qt::Key_Backspace;
        if (modifiers == Qt::NoModifier && (printable || editing) && haveRow) {
            QLineEdit* edit =
                m_tagRowWidgets[m_selectedRowIndex]->findChild<QLineEdit*>(u"ComposerTagEdit"_s);
            if (edit) {
                edit->setFocus(Qt::OtherFocusReason);
                if (printable)
                    edit->selectAll(); // a printable key replaces the tag
                else
                    edit->setCursorPosition(int(edit->text().size()));
                QApplication::sendEvent(edit, key);
                return true;
            }
        }
    }

    if (watched == m_popout) {
        if (event->type() == QEvent::Show) {
            m_previewLabel->hide();
            repositionFloats();
        } else if (event->type() == QEvent::Hide) {
            // Not over the states grid.
            if (!m_currentPixmap.isNull() && !m_statesViewActive) {
                m_previewLabel->show();
                repositionFloats();
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

} // namespace tc
