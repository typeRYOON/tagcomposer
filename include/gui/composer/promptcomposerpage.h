#pragma once
#include <core/facetindex.h>
#include <core/ruleengine.h>
#include <core/savedstate.h>
#include <core/taggroups.h>
#include <core/variableindex.h>
#include <core/promptpipeline.h>
#include <core/danbooruindex.h>
#include <core/workflowmanager.h>
#include <core/workflowinputcache.h>
#include <gui/widgets/tagsearchbar.h>
#include <QWidget>
#include <QSet>
#include <QMap>
#include <QHash>
#include <QStackedWidget>
#include <QPushButton>
#include <QLabel>
#include <QImage>
#include <QPixmap>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSpinBox>

class QVBoxLayout;
class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QMenu;
class QAction;
class QLineEdit;

namespace core {
class EntryModel;
}

namespace gui {

class ComposerScrollArea;
class PreviewClickLabel;
class StatesListWidget;
class WorkflowDropList;

class PromptComposerPage : public QWidget {
    Q_OBJECT
public:
    explicit PromptComposerPage(core::PromptPipeline* pipeline, core::RuleEngine* rules,
                                const core::TagGroupIndex& groups, QWidget* parent = nullptr);

    void setDanbooruIndex(core::DanbooruIndex* index);
    void setVariableIndex(core::VariableIndex* index);
    void setWorkflowManager(core::WorkflowManager* wm, const QString& savePath);
    void setStatesDir(const QString& dir);
    void setEntryModel(core::EntryModel* model);
    void setInputCache(core::WorkflowInputCache* cache)
    {
        m_inputCache = cache;
    }
    void setQuickFacets(const QString& characterFacet, const QString& copyrightFacet,
                        const QString& triggerWordFacet, const QString& styleFacet);

    QString currentPromptString(bool forJson) const;
    // Returns the user-typed active tags plus any rule-injected tag names
    // from the last pipeline run. Used by the facet editor's "undefined in
    // composer" list so rule-introduced tags without defs aren't missed.
    QList<QString> currentActiveTags() const;

    // Synchronously runs the pipeline for an arbitrary tag list and returns
    // the prompt string. Used by the batch runner to build per-entry prompts
    // without disturbing composer state. Applies user weights from m_tagWeights
    // for any tag that appears there; re-orders groups via groups.fct.
    QString computePromptForTags(const QList<QString>& tags, bool forJson) const;

    // Same as currentPromptString but with `extraTags` unioned on top of the
    // composer's current effective tag set (active minus deactivated). Used
    // by the batch runner to build "composer state + this entry's tags".
    QString computePromptWithExtraTags(const QList<QString>& extraTags, bool forJson) const;

    int currentPromptCount() const;

    void saveSession(const QString& path) const;
    void restoreSession(const QString& path);

public slots:
    void triggerRun();
    void loadPipeline(int entryId, int imageIdx, const QList<QString>& tags);
    void onPipelineReady(QList<core::CategoryGroup> groups);
    void onEntryTagAdded(int entryId, int imageIdx, const QString& tag);
    void onEntryTagRemoved(int entryId, int imageIdx, const QString& tag);
    // Hooked to EntryModel::entryDeleted. Drops every push belonging to
    // the deleted entry (and any tags only those pushes claimed) plus
    // the entry's LoRA uuid from the active set, so the composer doesn't
    // keep stale state pointing at a now-gone entry.
    void onEntryDeleted(int32_t entryId, const QString& uuid);
    // Hooked to EntryModel::imageRemovedFromEntry. Drops the push for the
    // removed slot (and any tags only that push claimed), then shifts any
    // higher-indexed pushes for the same entry down by one to track the
    // model's images.removeAt() reindexing.
    void onImageRemoved(int32_t entryId, int imageIdx);
    void setPreviewImage(const QImage& image);
    void setOutputFolderPattern(const QString& pattern);
    void setTempFolder(const QString& folder);
    void setActiveLoraUuids(const QList<QString>& uuids);
    void repush();
    // Posts a single queued repush per event-loop pass. Multiple callers in
    // the same pass (e.g. pasting a comma-separated tag list that fires
    // entryTagAdded once per tag) collapse into one pipeline run.
    void queueRepush();

signals:
    void activeGroupsChanged(QMap<int, QList<int>> activeGroups);
    void wikiRequested(const QString& tag);
    void facetEditorRequested(const QString& tag);
    void runRequested(int count);
    void interruptRequested();
    void clearPendingRequested();
    void workflowEditorRequested();
    void workflowVarsChanged();
    void statusMessageRequested(const QString& message);
    void loraUuidsRestored(QList<QString> uuids);
    // Right-click "Quick add as character/copyright" - AppMainWindow
    // mutates FacetIndex, persists, and triggers a facet reload.
    void quickFacetRequested(const QString& tag, const QString& facetName);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void rebuildGroupsDisplay(const QList<core::PipelineTag>& flat);
    // Performs the actual layout swap for a flat tag list. Split from
    // rebuildGroupsDisplay so it can run synchronously OR from the fade-out
    // finished handler, with the same end result either way.
    void applyGroupsRebuild(const QList<core::PipelineTag>& flat);
    void fadePreviewInset(qreal target);
    void applyTagFilter();
    void rebuildRulesSidebar();
    void rebuildVarsSidebar();
    void rebuildWorkflowList();
    void rebuildStatesList();
    void repositionFloats();
    void reloadRules();
    void reloadVars();
    void saveCurrentState();
    void overwriteState(int row);
    // Fills `state` with a snapshot of the current composer/workflow data.
    // Leaves id, name, and previewImagePath untouched - those are owned by
    // the caller (new save vs. overwriting an existing slot).
    void captureCurrentState(core::SavedState& state) const;
    void restoreState(const core::SavedState& state);
    void showStatePreview(int row);
    void hideStatePreview();

    QWidget* makeTagRow(const core::PipelineTag& pt);

    // Append the per-tag "Quick add as character/copyright/trigger word/style"
    // section to `menu` if any quick-facet preference is configured. Returns
    // a map from each appended QAction* to the facet name it represents, for
    // dispatch in the menu's exec() handler.
    QHash<QAction*, QString> addQuickFacetActions(QMenu& menu) const;

    // Tags carrying $VAR$ tokens get their weight stored by the *source*
    // form so the user's weight survives a variable-value change (which
    // rewrites pt.tag but not pt.sourceTag).
    static QString weightKeyOf(const core::PipelineTag& pt)
    {
        return pt.sourceTag.isEmpty() ? pt.tag : pt.sourceTag;
    }

    // Re-bucket a flat tag list into CategoryGroups in the user-defined
    // display order from `groups`. Tags with result == Deactivated are
    // dropped - callers that want to surface them separately must filter
    // first. Used by both the prompt builders and the on-screen layout.
    static QList<core::CategoryGroup> bucketForOutput(const QList<core::PipelineTag>& flat,
                                                      const core::TagGroupIndex& groups);

    // Round-trip m_activePushes against the portable {uuid, imageFileName,
    // tags} form. Unresolved-runtime-id pushes are skipped on dump; load
    // returns how many incoming pushes couldn't be resolved (entry deleted
    // between save and restore).
    QList<core::EntryPush> dumpActivePushes() const;
    int loadActivePushes(const QList<core::EntryPush>& pushes);

    // Rewrites every $foo$ in oldKey to $newVarName$ (or strips them when
    // newVarName is empty), updates the active set, and triggers a repush.
    void replaceTagVariable(const QString& oldKey, const QString& newVarName);

    // Rewrites every occurrence of `oldKey` inside m_activePushes to `newKey`.
    // When `newKey` is empty (rename collided with an existing active tag),
    // the old entry is dropped so the push doesn't continue to claim a tag
    // that's already owned elsewhere.
    void renamePushTag(const QString& oldKey, const QString& newKey);

    core::PromptPipeline* m_pipeline;
    core::RuleEngine* m_rules;
    core::TagGroupIndex m_groups;
    core::VariableIndex* m_varIndex = nullptr;
    core::WorkflowManager* m_wfManager = nullptr;
    core::EntryModel* m_entryModel = nullptr;
    core::WorkflowInputCache* m_inputCache = nullptr;
    QString m_wfSavePath;

    // Empty when the user hasn't opted in via settings - menu items hidden.
    QString m_quickCharFacet;
    QString m_quickCopyFacet;
    QString m_quickTriggerFacet;
    QString m_quickStyleFacet;

    QString m_filterQuery;
    bool m_undefinedOnly = false;
    bool m_repushPending = false;
    QList<QString> m_activeLoraUuids;
    QList<QString> m_activeTags;
    QSet<QString> m_activeTagSet;
    QSet<QString> m_deactivatedTags; // tags kept in list but excluded from pipeline
    QList<core::PipelineTag> m_lastResult;
    QHash<QString, float>
        m_tagWeights; // weight keyed via weightKeyOf - sourceTag wins when present

    QHash<qint64, QList<QString>> m_activePushes;

    // UI - main area
    TagSearchBar* m_searchBar;
    QPushButton* m_undefinedToggleBtn = nullptr;
    QStackedWidget* m_mainStack;
    QWidget* m_groupsContainer;
    QVBoxLayout* m_groupsLayout;
    QWidget* m_centerBg;

    // Floating preview widgets (bottom-right, above control bar)
    PreviewClickLabel* m_previewLabel = nullptr;        // floating preview image
    QGraphicsOpacityEffect* m_previewInsetFx = nullptr; // opacity effect for fade
    QPropertyAnimation* m_previewInsetFade = nullptr;   // animation driving the effect
    QWidget* m_controlBar = nullptr;                    // floating control bar below preview
    QPushButton* m_runBtn;
    QSpinBox* m_promptCountSpin;
    QPushButton* m_interruptBtn;

    // Preview popout window (created on first click, Qt::Window)
    QWidget* m_popout = nullptr;
    QPixmap m_currentPix;
    QString m_outputFolderPattern;
    QString m_tempFolder;

    // UI - workflow/states sidebar
    WorkflowDropList* m_wfList = nullptr;
    StatesListWidget* m_statesList = nullptr;
    QLineEdit* m_wfFilter = nullptr;
    QLineEdit* m_statesFilter = nullptr;
    QStackedWidget* m_wfStateStack = nullptr;
    QPushButton* m_wfEditBtnRef = nullptr;
    QPushButton* m_saveStateBtn = nullptr;
    QLabel* m_statesPreviewPopup = nullptr;

    // State management
    core::StateManager m_stateManager;
    QString m_statesDir;
    bool m_suppressRuleSave = false;
    // When set, the next rebuildGroupsDisplay swaps content behind a fade
    // animation instead of doing it in-place. Cleared by rebuildGroupsDisplay.
    bool m_freezeNextRebuild = false;

    // Fade animation that hides the rebuild flicker. Effect is attached to
    // m_mainStack so the search bar above it stays interactive throughout.
    QGraphicsOpacityEffect* m_mainStackFx = nullptr;
    QPropertyAnimation* m_mainStackFade = nullptr;

    // UI - rule sidebar
    QWidget* m_rulesContainer;
    QVBoxLayout* m_rulesLayout;

    // UI - variable sidebar section
    QWidget* m_varsContainer;
    QVBoxLayout* m_varsLayout;

    // Copy-to-clipboard button area
    QPushButton* m_copyBtn;

    // Category nav panel (top-right float)
    QWidget* m_categoryNav = nullptr;
    QPushButton* m_clearBtn = nullptr;
    ComposerScrollArea* m_groupsScroll = nullptr;
    QMap<QString, QWidget*> m_groupHeaders; // display name -> header label
};

} // namespace gui
