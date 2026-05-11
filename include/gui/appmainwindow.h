#pragma once
#include <core/entrymodel.h>
#include <core/danbooruindex.h>
#include <core/facetindex.h>
#include <core/ruleengine.h>
#include <core/taggroups.h>
#include <core/variableindex.h>
#include <core/promptpipeline.h>
#include <core/comfyuiclient.h>
#include <core/prompthistory.h>
#include <core/workflowmanager.h>
#include <core/workflowinputcache.h>
#include <core/autotaggerlibrary.h>
#include <core/soundplayer.h>
#include <utils/appsettings.h>
#include <QMainWindow>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QSet>
#include <QStackedWidget>
#include <functional>
#include <memory>

namespace core {
class UpdateChecker;
}

namespace gui {
class HomePage;
class TileViewPage;
class PromptComposerPage;
class FacetEditorPage;
class TagWikiPage;
class SettingsPage;
class WorkflowEditPage;
class OutputViewerPage;
class DatasetHelpersPage;
class PromptHistoryPage;
class StatusBar;
class DanmakuOverlay;
class WindowChrome;

class AppMainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit AppMainWindow(QWidget* parent = nullptr);
    ~AppMainWindow() = default;

    AppMainWindow(const AppMainWindow&) = delete;
    AppMainWindow& operator=(const AppMainWindow&) = delete;
    AppMainWindow(AppMainWindow&&) = delete;
    AppMainWindow& operator=(AppMainWindow&&) = delete;

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void reloadFacets();
    void applyComfySettings();
    void applyQuickFacet(const QString& tag, const QString& facetName);
    void runBatch(const QString& query);
    void ensureImageInputsUploaded(std::function<void()> done);

    // Placeholders of Image vars in the selected workflow that have no
    // image picked yet (imageUuid empty). Used to block runs that would
    // emit an empty filename token into the workflow JSON.
    QStringList unloadedImageInputs() const;

    // Returns human-readable problems with the workflow template that should
    // block a run: variables whose placeholder/token never appears in `tmpl`,
    // stray __dunder__ tokens in `tmpl` that no handler (vars, positive,
    // lora stack) will substitute, and (when activeLoraCount > 0) missing
    // __lora_name_N__ slots that would silently drop the user's selection.
    // Built-ins like __positive__ are never required to be present.
    // Empty list = OK to send.
    QStringList workflowTemplateIssues(const QString& tmpl, int activeLoraCount) const;

    // Snapshot composer/workflow state, append a record to m_promptHistory,
    // and push the rendered JSON to ComfyUI. Centralises history capture so
    // composer Run and batch share the same code path.
    void recordAndQueue(const QString& renderedJson, const QString& positivePrompt,
                        const QList<core::LoraConfig>& healed, int batchEntryId = -1);

    // Sweep WorkflowInputCache: drop entries no workflow var references.
    void clearUnusedInputs();

    // Drop tag definitions with no facets, or not in Danbooru and unused
    // by every entry. In-memory only - persists at next saveDefinitions().
    void purgeTagDefinitions();

    // Strip facet entries from tag definitions whose name isn't in the
    // current facets.fct schema (case-sensitive). Catches leftovers from
    // hand-edited tag_definitions.fct or renamed quick-facet settings.
    void purgeUnknownFacets();

    // Heal each LoRA's (rootKey, relPath) against the current dirs and
    // mirror changes back to the source entry (matched by sha256).
    void healLoraStackInPlace(QList<core::LoraConfig>& stack);

    // After the last queued ComfyUI job finishes, pin the freshest temp-folder
    // image as the inline preview so it shows the decoded output, not a latent.
    void loadFinalPreview();
    bool m_pendingFinalLoad = false;
    int m_lastQueueCount = 0;
    // Suppress the next final-image load: m_skipNextFinalLoad on interrupt
    // (output is stale), m_skipFinalOnPendingClear on clearPending (only the
    // dropped queue, not a real finish).
    bool m_skipNextFinalLoad = false;
    bool m_skipFinalOnPendingClear = false;

    core::EntryModel* m_entryModel;
    QStackedWidget* m_pages;

    gui::HomePage* m_homePage = nullptr;
    core::UpdateChecker* m_updateChecker = nullptr;
    gui::TileViewPage* m_tileViewPage = nullptr;
    gui::PromptComposerPage* m_composerPage = nullptr;
    gui::FacetEditorPage* m_facetEditorPage = nullptr;
    gui::TagWikiPage* m_wikiPage = nullptr;
    gui::SettingsPage* m_settingsPage = nullptr;
    gui::WorkflowEditPage* m_workflowEditPage = nullptr;
    gui::OutputViewerPage* m_outputViewerPage = nullptr;
    gui::DatasetHelpersPage* m_datasetHelpersPage = nullptr;
    gui::PromptHistoryPage* m_promptHistoryPage = nullptr;
    gui::StatusBar* m_statusBar = nullptr;
    gui::DanmakuOverlay* m_danmakuOverlay = nullptr;
    gui::WindowChrome* m_chrome = nullptr;

    core::DanbooruIndex* m_danbooruIndex = nullptr;
    core::ComfyUiClient* m_comfyClient = nullptr;
    core::WorkflowInputCache* m_inputCache = nullptr;
    QSet<QString> m_uploadedThisSession;

    // Cache of the comfy values applyComfySettings last actually reconnected
    // on, so unrelated settings edits don't bounce the WebSocket.
    bool m_lastComfyEnabled = false;
    QString m_lastComfyHost;

    utils::AppSettings m_settings;

    QList<core::LoraConfig> m_activeLoraStack;
    QList<QString> m_activeLoraUuids;

    // Pipeline owned here as values; PromptPipeline holds pointers to them.
    core::FacetIndex m_facetIndex;
    core::RuleEngine m_ruleEngine;
    core::TagGroupIndex m_tagGroupIndex;
    core::VariableIndex m_varIndex;
    core::WorkflowManager m_workflowManager;
    core::PromptPipeline* m_pipeline = nullptr;
    core::PromptHistory* m_promptHistory = nullptr;

    // One library per app: shares the Ort::Env and cached sessions.
    std::unique_ptr<core::AutoTaggerLibrary> m_taggerLibrary;

    // Owns the QSoundEffect cache; reachable via core::SoundPlayer::instance().
    core::SoundPlayer* m_soundPlayer = nullptr;
};

} // namespace gui
