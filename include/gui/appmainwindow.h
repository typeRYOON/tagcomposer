#pragma once
#include <core/entrymodel.h>
#include <core/danbooruindex.h>
#include <core/facetindex.h>
#include <core/ruleengine.h>
#include <core/taggroups.h>
#include <core/variableindex.h>
#include <core/promptpipeline.h>
#include <core/comfyuiclient.h>
#include <core/workflowmanager.h>
#include <core/workflowinputcache.h>
#include <utils/appsettings.h>
#include <QMainWindow>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QSet>
#include <QStackedWidget>
#include <functional>

namespace gui {
class TileViewPage;
class PromptComposerPage;
class FacetEditorPage;
class TagWikiPage;
class SettingsPage;
class WorkflowEditPage;
class DatasetHelpersPage;
class StatusBar;
class DanmakuOverlay;
class TitleBar;

class AppMainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit AppMainWindow(QWidget* parent = nullptr);
    ~AppMainWindow() = default;

    AppMainWindow(const AppMainWindow&)            = delete;
    AppMainWindow& operator=(const AppMainWindow&) = delete;
    AppMainWindow(AppMainWindow&&)                 = delete;
    AppMainWindow& operator=(AppMainWindow&&)      = delete;

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void reloadFacets();
    void applyComfySettings();
    // Adds `facetName` to `tag`'s facet list (no-op if already present),
    // persists, and reloads. Shared by composer + entry-panel quick-add menus.
    void applyQuickFacet(const QString& tag, const QString& facetName);

    // Resolve query to entries, build per-entry prompts (composer state ∪
    // each entry's tags) and push them all to ComfyUI. Fire-and-forget.
    void runBatch(const QString& query);

    // Walks the selected workflow's image vars and uploads every uuid not
    // already pushed this session. Calls done() once every upload settled.
    void ensureImageInputsUploaded(std::function<void()> done);

    // After the last comfy job in a queue completes, load the newest image
    // from the temp folder and pin it as the inline preview (so the preview
    // shows the actual decoded output, not the last latent step).
    void loadFinalPreview();
    bool m_pendingFinalLoad   = false;
    int  m_lastQueueCount     = 0;
    // Set on interrupt; consumed by the next queue-decrement event to
    // suppress the final-image load (the just-killed prompt's output either
    // doesn't exist or is stale).
    bool m_skipNextFinalLoad  = false;
    // Set on clearPending; suppresses the next queue-decrement final load
    // (which is the cleared-pending drop, not a finished prompt) but leaves
    // m_pendingFinalLoad alone so the still-running prompt still loads when
    // it actually finishes.
    bool m_skipFinalOnPendingClear = false;

    core::EntryModel*   m_entryModel;
    QStackedWidget*      m_pages;

    gui::TileViewPage*        m_tileViewPage      = nullptr;
    gui::PromptComposerPage*  m_composerPage      = nullptr;
    gui::FacetEditorPage*     m_facetEditorPage   = nullptr;
    gui::TagWikiPage*         m_wikiPage          = nullptr;
    gui::SettingsPage*        m_settingsPage      = nullptr;
    gui::WorkflowEditPage*    m_workflowEditPage  = nullptr;
    gui::StatusBar*           m_statusBar         = nullptr;
    gui::DanmakuOverlay*      m_danmakuOverlay    = nullptr;
    gui::TitleBar*            m_titleBar          = nullptr;
    QWidget*                  m_frame             = nullptr;
    QWidget*                  m_resizeOverlay     = nullptr;
    QWidget*                  m_resizeOutline     = nullptr;  // lazy

    // Active outline-resize drag (AIMP-style: preview rectangle while
    // dragging, commit geometry on release). m_dragEdges == 0 means no drag.
    Qt::Edges                 m_dragEdges{};
    QRect                     m_dragStartGeo;
    QPoint                    m_dragStartGlobal;

    void  beginResizeDrag(Qt::Edges edges, const QPoint& globalStart);
    void  updateResizeOutline(const QPoint& globalNow);
    void  endResizeDrag(const QPoint& globalNow);
    QRect computeResizeGeometry(const QPoint& globalNow) const;

    core::DanbooruIndex*      m_danbooruIndex = nullptr;
    core::ComfyUiClient*      m_comfyClient   = nullptr;
    core::WorkflowInputCache* m_inputCache    = nullptr;
    QSet<QString>             m_uploadedThisSession;

    utils::AppSettings m_settings;

    QList<core::LoraConfig> m_activeLoraStack;
    QList<QString>          m_activeLoraUuids;

    // Pipeline stack (value types stored here; pipeline holds pointers to them)
    core::FacetIndex      m_facetIndex;
    core::RuleEngine      m_ruleEngine;
    core::TagGroupIndex   m_tagGroupIndex;
    core::VariableIndex   m_varIndex;
    core::WorkflowManager m_workflowManager;
    core::PromptPipeline* m_pipeline = nullptr;
};

}
