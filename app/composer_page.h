#pragma once
#include <core/composer_store.h>
#include <core/pipeline.h>
#include <core/profiles.h>
#include <core/prompt.h>
#include <core/rule.h>
#include <core/run.h>
#include <core/saved_state.h>
#include <core/settings.h>
#include <core/tag_facets.h>
#include <core/tag_groups.h>
#include <core/variables.h>
#include <QColor>
#include <QHash>
#include <QImage>
#include <QMap>
#include <QPixmap>
#include <QSet>
#include <QWidget>

class QAction;
class QComboBox;
class QGraphicsOpacityEffect;
class QLabel;
class QLineEdit;
class QMenu;
class QPropertyAnimation;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTimer;
class QVBoxLayout;

namespace tc {

class AppData;
class CategoryNavPanel;
class ComfyClient;
class ComposerScrollArea;
class EntryStore;
class PreviewClickLabel;
class PreviewPopoutWindow;
class PromptHistory;
class StatesGridView;
class TagPreviewPopup;
class TagSearchBar;
class WorkflowDropList;
class WorkflowInputCache;

// The composer: tag list, rules/workflows/variables sidebar and floating
// controls. The document lives in ComposerStore; this page renders it and
// routes every change through the store.
class ComposerPage : public QWidget {
    Q_OBJECT

public:
    ComposerPage(ComposerStore& store, AppData& data, EntryStore& entries,
                 WorkflowInputCache& cache, ComfyClient& comfy, PromptHistory& history,
                 QWidget* parent = nullptr);

    QString currentPromptString() const;

    // Active tags plus tags rules injected on the last evaluation.
    QStringList currentActiveTags() const;

    // One-off pipeline runs that leave the document alone.
    QString computePromptForTags(const QStringList& tags) const;
    QString computePromptWithExtraTags(const QStringList& extraTags) const;

    int currentPromptCount() const;

    void saveSession(const QString& path) const;
    void restoreSession(const QString& path);

    // The caller fills in id, name and previewImagePath.
    SavedState currentSnapshot() const;
    void restoreFromSnapshot(const SavedState& state);

    // Saves state under a new id without prompting (history -> state).
    void appendSnapshotAsState(SavedState state, const QString& displayName = {});

    // Image vars referenced by saved states, kept by cache purges.
    QList<WorkflowVar> imageVarsFromStates() const;

public slots:
    // Called once the data dir has loaded.
    void reloadAll();

    void refresh();

    // Shared by the buttons and shortcuts.
    void undo();
    void redo();

    void reloadWorkflows();
    void reloadRules();
    void reloadVars();
    void reloadProfiles();
    void applySettings();

    void triggerRun();
    void setPreviewImage(const QImage& image);
    void setComfyProgress(int step, int total);
    void setComfyActiveCount(int count);

    void togglePush(const QString& entryUuid, const QString& imageFile, const QStringList& tags);

signals:
    void statusMessage(const QString& message);
    void wikiRequested(const QString& tag);
    void facetEditorRequested(const QString& tag);
    void workflowEditorRequested();
    void workflowVarsChanged();
    void pushesChanged();

    // Right-click quick add; the shell writes the definition.
    void quickFacetRequested(const QString& tag, const QString& facetName);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // ---- Build
    QWidget* buildCentre();
    QWidget* buildSidebar();
    void buildFloats();

    // ---- Render
    void rebuildGroupsDisplay(const QList<PipelineTag>& tags);
    void applyGroupsRebuild(const QList<PipelineTag>& tags);
    void applyTagFilter();
    QWidget* makeTagRow(const PipelineTag& tag);
    void setSelectedRow(int index);
    void repositionFloats();
    void fadePreviewInset(qreal target);
    int composerStackIndex() const;
    QGraphicsOpacityEffect* stackChildFx(int index) const;

    // Coalesces rebuilds into the next event-loop turn.
    void queueRebuild();

    // ---- Sidebar
    void rebuildRulesSidebar();
    void rebuildVarsSidebar();
    void rebuildProfilesSidebar();
    void rebuildWorkflowList();
    void rebuildStatesList();
    void promptAddRule();

    // ---- Filters
    void showPushedFilterMenu();
    void setPushedFilter(bool on, const QString& key);
    void updatePushedButton();
    QString pushLabel(const QString& key) const;

    // ---- Custom tags
    void promptAddCustomTag(const QString& groupName);
    void addCustomTagMenu(QMenu& menu);
    QHash<QAction*, QString> addQuickFacetActions(QMenu& menu) const;
    TagPreviewPopup* tagPreviewPopup();

    // ---- Profiles
    void applyActiveProfiles(bool persist, bool refreshNow);
    void applyProfileStamp(const SavedState& state, bool refreshNow);

    // ---- States
    void saveCurrentState();
    void overwriteState(int row);
    void captureCurrentState(SavedState& state) const;
    void restoreState(const SavedState& state);
    void setStatesViewActive(bool active);
    void leaveStatesViewMode();

    // ---- Run
    void run();
    const Workflow* selectedWorkflow() const;
    void report(const QString& error);

    // Rewrites $foo$ tokens in a tag to $newVarName$; an empty name strips them.
    void replaceTagVariable(const QString& oldKey, const QString& newVarName);

    QList<PipelineTag> evaluateTags(const QStringList& tags) const;
    PipelineContext pipelineContext() const;

    ComposerStore* m_store = nullptr;
    AppData* m_data = nullptr;
    EntryStore* m_entries = nullptr;
    WorkflowInputCache* m_cache = nullptr;
    ComfyClient* m_comfy = nullptr;
    PromptHistory* m_history = nullptr;

    // AppData's groups in profile order.
    TagGroups m_groups;
    QList<FacetFormat> m_facetFormats; // the effective list
    ProfileIndex m_profiles;
    QString m_oneOffGroupLabel;
    QString m_oneOffFormatLabel;

    StateManager m_stateManager;
    QString m_statesDir;

    QString m_filterQuery;
    bool m_undefinedOnly = false;
    bool m_pushedOnly = false;
    QString m_pushedFilterKey; // empty means every push
    bool m_repushPending = false;

    QList<PipelineTag> m_lastResult;
    QSet<QString> m_activeTagSet; // for the search bar

    // Section each tag was deactivated in, so its row stays there.
    QHash<QString, QString> m_deactivatedCategory;

    // ---- Centre
    TagSearchBar* m_searchBar = nullptr;
    QPushButton* m_undefinedToggleBtn = nullptr;
    QStackedWidget* m_mainStack = nullptr;
    QWidget* m_groupsContainer = nullptr;
    QVBoxLayout* m_groupsLayout = nullptr;
    ComposerScrollArea* m_groupsScroll = nullptr;
    QMap<QString, QWidget*> m_groupHeaders;

    // ---- Floats
    PreviewClickLabel* m_previewLabel = nullptr;
    TagPreviewPopup* m_tagPreviewPopup = nullptr; // built on first use
    QGraphicsOpacityEffect* m_previewInsetFx = nullptr;
    QPropertyAnimation* m_previewInsetFade = nullptr;
    QWidget* m_controlBar = nullptr;
    QPushButton* m_undoBtn = nullptr;
    QPushButton* m_redoBtn = nullptr;
    QPushButton* m_runBtn = nullptr;
    QPushButton* m_interruptBtn = nullptr;
    QPushButton* m_copyBtn = nullptr;
    QSpinBox* m_promptCountSpin = nullptr;
    CategoryNavPanel* m_categoryNav = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QPushButton* m_pushedBtn = nullptr;
    QList<QWidget*> m_composerFloats;

    PreviewPopoutWindow* m_popout = nullptr;
    QPixmap m_currentPixmap;
    QString m_outputFolderPattern;
    QString m_tempFolder;

    // ---- Sidebar
    QWidget* m_sidebar = nullptr;
    QWidget* m_rulesContainer = nullptr;
    QVBoxLayout* m_rulesLayout = nullptr;
    QWidget* m_varsContainer = nullptr;
    QVBoxLayout* m_varsLayout = nullptr;
    QComboBox* m_groupProfileBox = nullptr;
    QComboBox* m_formatProfileBox = nullptr;
    WorkflowDropList* m_workflowList = nullptr;
    QLineEdit* m_workflowFilter = nullptr;
    QPushButton* m_saveStateBtn = nullptr;
    QPushButton* m_statesToggleBtn = nullptr;

    // ---- States view
    StatesGridView* m_statesGrid = nullptr;
    QWidget* m_statesView = nullptr;
    QLineEdit* m_statesFilter = nullptr;
    QLabel* m_statesEmptyHint = nullptr;
    bool m_statesViewActive = false;

    // One effect per stack child; a single one on the stack goes stale on swaps.
    QGraphicsOpacityEffect* m_emptyHintFx = nullptr;
    QGraphicsOpacityEffect* m_groupsFx = nullptr;
    QGraphicsOpacityEffect* m_statesViewFx = nullptr;
    QPropertyAnimation* m_mainStackFade = nullptr;

    // The next rebuild fades through zero instead of swapping in place.
    bool m_freezeNextRebuild = false;

    // Lets refresh() tell a weight nudge from a change that needs a rebuild.
    ComposerDoc m_lastDoc;
    bool m_weightFromSpin = false;

    bool m_rebuildPending = false;

    int m_lastComfyStep = 0;
    int m_lastComfyTotal = 0;
    int m_lastComfyActive = 0;

    // For Up/Down row navigation.
    QList<QWidget*> m_tagRowWidgets;
    int m_selectedRowIndex = -1;

    QTimer* m_filterDebounce = nullptr;
};

} // namespace tc
