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
class QTimer;

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
    // Active tags plus rule-injected names from the last run, so the facet
    // editor's "undefined in composer" list catches rule-introduced tags.
    QList<QString> currentActiveTags() const;

    // Sync pipeline run; batch path uses this without disturbing composer
    // state. Applies m_tagWeights to overlapping tags.
    QString computePromptForTags(const QList<QString>& tags, bool forJson) const;

    // currentPromptString with extraTags unioned on top of (active - deactivated).
    QString computePromptWithExtraTags(const QList<QString>& extraTags, bool forJson) const;

    int currentPromptCount() const;

    void saveSession(const QString& path) const;
    void restoreSession(const QString& path);

    // Image-typed workflow vars rebuilt from every saved state. Lets the
    // input-cache purge keep entries that a state restore would re-reference.
    QList<core::WorkflowVar> imageVarsFromStates() const;

public slots:
    void triggerRun();
    // No-op when only the session baseline remains.
    void undo();
    void redo();
    void loadPipeline(int entryId, int imageIdx, const QList<QString>& tags);
    void onPipelineReady(QList<core::CategoryGroup> groups);
    void onEntryTagAdded(int entryId, int imageIdx, const QString& tag);
    void onEntryTagRemoved(int entryId, int imageIdx, const QString& tag);
    // Drop pushes + LoRA uuid for the deleted entry; tags claimed only by
    // those pushes also drop.
    void onEntryDeleted(int32_t entryId, const QString& uuid);
    // Drop the push for the removed slot, then shift higher-indexed pushes
    // for the same entry down by one to track images.removeAt() reindexing.
    void onImageRemoved(int32_t entryId, int imageIdx);
    void setPreviewImage(const QImage& image);
    void setOutputFolderPattern(const QString& pattern);
    void setTempFolder(const QString& folder);
    void setActiveLoraUuids(const QList<QString>& uuids);
    // Forwarded from ComfyUiClient by AppMainWindow so the popout's mirrored
    // status bar tracks progress / queue depth without a direct dependency.
    void setComfyProgress(int step, int total);
    void setComfyActiveCount(int count);
    void repush();
    // Coalesces multiple same-pass callers (e.g. pasting comma-separated
    // tags that fire entryTagAdded once per tag) into one pipeline run.
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
    // Layout swap; split out so rebuildGroupsDisplay can run it sync or
    // from the fade-out finished handler.
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
    // Snapshot composer/workflow data into `state`. Leaves id, name,
    // previewImagePath alone - caller owns those.
    void captureCurrentState(core::SavedState& state) const;
    void restoreState(const core::SavedState& state);
    void showStatePreview(int row);
    void hideStatePreview();

    QWidget* makeTagRow(const core::PipelineTag& pt);

    // Returns appended QAction* -> facet name for menu.exec() dispatch.
    QHash<QAction*, QString> addQuickFacetActions(QMenu& menu) const;

    // $VAR$ tags key on sourceTag so the user's weight survives a
    // variable-value change (which rewrites pt.tag but not pt.sourceTag).
    static QString weightKeyOf(const core::PipelineTag& pt)
    {
        return pt.sourceTag.isEmpty() ? pt.tag : pt.sourceTag;
    }

    // Re-bucket a flat tag list into CategoryGroups in `groups` order.
    // Drops Deactivated tags - callers that want them must filter first.
    static QList<core::CategoryGroup> bucketForOutput(const QList<core::PipelineTag>& flat,
                                                      const core::TagGroupIndex& groups);

    // Round-trip m_activePushes against {uuid, imageFileName, tags}.
    // dump skips unresolved runtime ids; load returns the unresolved count.
    QList<core::EntryPush> dumpActivePushes() const;
    int loadActivePushes(const QList<core::EntryPush>& pushes);

    // Rewrites every $foo$ in oldKey to $newVarName$ (or strips them when
    // newVarName is empty), updates the active set, repushes.
    void replaceTagVariable(const QString& oldKey, const QString& newVarName);

    // Rewrites oldKey -> newKey inside m_activePushes. Empty newKey (collision)
    // drops the entry so the push stops claiming a tag owned elsewhere.
    void renamePushTag(const QString& oldKey, const QString& newKey);

    // Undo/redo - see promptcomposerpage_undo.cpp.
    // Called after a user action commits; same `kind` within kUndoCoalesceMs
    // replaces the top instead of pushing (keeps slider/typing spam out).
    void captureUndoSnapshot(const QString& kind = QString());
    // Clears both stacks; seeds undo with a baseline of the current state.
    void rebaselineUndo();
    void clearRedoStack();
    void updateUndoButtons();

    struct UndoEntry {
        core::SavedState snapshot;
        QString kind;
        qint64 timestamp = 0;
    };
    QList<UndoEntry> m_undoStack;
    QList<UndoEntry> m_redoStack;
    bool m_suppressUndoCapture = false;
    static constexpr int kUndoStackCap = 50;
    static constexpr qint64 kUndoCoalesceMs = 800;

    core::PromptPipeline* m_pipeline;
    core::RuleEngine* m_rules;
    core::TagGroupIndex m_groups;
    core::DanbooruIndex* m_danbooruIndex = nullptr;
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
    // Category captured at deactivation time so the row can keep showing in
    // its original section instead of being pooled into a "Deactivated"
    // group. Ephemeral - falls back to Uncategorized if missing.
    QHash<QString, QString> m_deactivatedCategory;
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
    QPushButton* m_undoBtn = nullptr;
    QPushButton* m_redoBtn = nullptr;
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
    // Next rebuildGroupsDisplay fades through 0 instead of swapping in place.
    bool m_freezeNextRebuild = false;

    // Effect on m_mainStack (not contentWidget) so the search bar stays
    // interactive while the tag list fades.
    QGraphicsOpacityEffect* m_mainStackFx = nullptr;
    QPropertyAnimation* m_mainStackFade = nullptr;

    // Cached so a freshly-opened popout can sync to the in-flight job state.
    int m_lastComfyStep = 0;
    int m_lastComfyTotal = 0;
    int m_lastComfyActive = 0;

    // Up/Down keyboard navigation across the groups list. m_tagRowWidgets is
    // rebuilt each applyGroupsRebuild; m_selectedRowIdx indexes into it.
    QList<QWidget*> m_tagRowWidgets;
    int m_selectedRowIdx = -1;
    void setSelectedRow(int idx);

    // Coalesces rapid search-bar keystrokes into one faded rebuild so the
    // composer doesn't tear from per-char applyTagFilter calls.
    QTimer* m_filterDebounceTimer = nullptr;

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
