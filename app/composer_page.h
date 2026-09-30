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

// The composer: the tag list in the middle, the rules / workflows /
// variables sidebar on the right, and floating controls over the bottom
// right corner.
//
// The document lives in ComposerStore, not here - active tags, weights,
// deactivations, pushes, custom facets and undo all belong to it. This page
// reads the document, renders it, and asks the store for every change.
class ComposerPage : public QWidget {
    Q_OBJECT

public:
    ComposerPage(ComposerStore& store, AppData& data, EntryStore& entries,
                 WorkflowInputCache& cache, ComfyClient& comfy, PromptHistory& history,
                 QWidget* parent = nullptr);

    // The prompt the current document produces.
    QString currentPromptString(bool forJson) const;

    // The active tags plus the names rules injected on the last run, so the
    // facet editor's undefined list catches a rule-introduced tag too.
    QStringList currentActiveTags() const;

    // A one-off pipeline run that does not disturb the document. The batch
    // runner uses it. Weights apply where the tags overlap.
    QString computePromptForTags(const QStringList& tags, bool forJson) const;
    QString computePromptWithExtraTags(const QStringList& extraTags, bool forJson) const;

    int currentPromptCount() const;

    void saveSession(const QString& path) const;
    void restoreSession(const QString& path);

    // The live composer and workflow state as a state object. The caller
    // fills in id, name and previewImagePath.
    SavedState currentSnapshot() const;
    void restoreFromSnapshot(const SavedState& state);

    // Appends `state` with a generated id and the given display name, with no
    // prompt. For turning a history entry into a state.
    void appendSnapshotAsState(SavedState state, const QString& displayName = {});

    // Every image-typed workflow var any saved state references, so a cache
    // purge keeps what a restore would need.
    QList<WorkflowVar> imageVarsFromStates() const;

public slots:
    // The page is built before the data dir is read, so this is what the
    // shell calls once everything has actually loaded.
    void reloadAll();

    // The .fct values are plain data with no signals, so the shell calls
    // these after a load or an external edit.
    void refresh();

    // Both report what they did. They are slots rather than direct store
    // connections so the buttons and the shortcuts share one path.
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

    // Pushes an entry image's tags in, or takes them out again.
    void togglePush(const QString& entryUuid, const QString& imageFile, const QStringList& tags);

signals:
    void statusMessage(const QString& message);
    void wikiRequested(const QString& tag);
    void facetEditorRequested(const QString& tag);
    void workflowEditorRequested();
    void workflowVarsChanged();
    void pushesChanged();

    // Right-click quick add: the shell mutates the definitions and saves.
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

    // Queues one rebuild of the tag list for the next event-loop turn.
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

    // Rewrites every $foo$ in a tag to $newVarName$, or strips them when the
    // name is empty.
    void replaceTagVariable(const QString& oldKey, const QString& newVarName);

    QList<PipelineTag> evaluateTags(const QStringList& tags) const;
    PipelineContext pipelineContext() const;

    ComposerStore* m_store = nullptr;
    AppData* m_data = nullptr;
    EntryStore* m_entries = nullptr;
    WorkflowInputCache* m_cache = nullptr;
    ComfyClient* m_comfy = nullptr;
    PromptHistory* m_history = nullptr;

    // groups.fct as loaded is in AppData; this is the profile-ordered copy
    // that actually does the bucketing.
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
    QSet<QString> m_activeTagSet; // what the search bar checks against

    // The section a tag was deactivated in, so its row keeps showing there
    // instead of being pooled into one Deactivated group. Ephemeral: a
    // missing entry just falls back to Uncategorized.
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

    // One opacity effect per stack child. A single effect on the stack itself
    // caches the source pixmap across a child swap and goes stale.
    QGraphicsOpacityEffect* m_emptyHintFx = nullptr;
    QGraphicsOpacityEffect* m_groupsFx = nullptr;
    QGraphicsOpacityEffect* m_statesViewFx = nullptr;
    QPropertyAnimation* m_mainStackFade = nullptr;

    // The next rebuild fades through zero instead of swapping in place.
    bool m_freezeNextRebuild = false;

    // The document as the list was last built from it, so refresh() can tell
    // a weight nudge from a change that needs the rows rebuilt.
    ComposerDoc m_lastDoc;
    bool m_weightFromSpin = false;

    // Set while a rebuild is already queued, so a burst of edits collapses
    // into one.
    bool m_rebuildPending = false;

    int m_lastComfyStep = 0;
    int m_lastComfyTotal = 0;
    int m_lastComfyActive = 0;

    // Up and down move between tag rows on the focused list.
    QList<QWidget*> m_tagRowWidgets;
    int m_selectedRowIndex = -1;

    QTimer* m_filterDebounce = nullptr;
};

} // namespace tc
