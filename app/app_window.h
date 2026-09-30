#pragma once
#include <app/app_data.h>
#include <QMainWindow>
#include <memory>

class QCloseEvent;
class QStackedWidget;

namespace tc {

class ComfyClient;
class ComposerPage;
class DatasetHelpersPage;
class TaggerLibrary;
class EntryViewerPage;
class FacetEditorPage;
class NavBar;
class OutputViewerPage;
class PromptHistory;
class PromptHistoryPage;
class SettingsPage;
class StatusBar;
class TagWikiPage;
class WorkflowEditPage;
class WorkflowInputCache;
class WindowChrome;

// The shell: frameless chrome, navbar, page stack, bottom info bar.
//
// Pages come from kPages. A page that has no implementation yet gets a
// placeholder, so the navbar and the stack are complete from the start and a
// real page is a one-line swap in buildPage().
class AppWindow : public QMainWindow {
    Q_OBJECT

public:
    // dataDir holds system/ and entry/.
    explicit AppWindow(const QString& dataDir, QWidget* parent = nullptr);

    // Out of line: m_taggers is a unique_ptr to a type this header only
    // forward-declares, and deleting it needs the definition.
    ~AppWindow() override;

protected:
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    QWidget* buildPage(int index);
    void updateTitle();
    // Shared by the composer and the cluster page: both offer the same
    // quick-add menu, and both want it written through at once.
    // Reads the just-rendered image back off disk and shows it. Comfy only
    // streams sampler previews, so the finished picture arrives this way.
    void loadFinalPreview();

    void addQuickFacet(const QString& tag, const QString& facet);
    void installShortcuts();
    void report(const QString& error);

    AppData m_data;
    QString m_dataDir;
    bool m_loaded = false;
    bool m_closing = false;
    bool m_sessionRestored = false;
    ComfyClient* m_comfy = nullptr;
    ComposerPage* m_composerPage = nullptr;
    SettingsPage* m_settingsPage = nullptr;
    FacetEditorPage* m_facetPage = nullptr;
    TagWikiPage* m_wikiPage = nullptr;
    PromptHistory* m_history = nullptr;
    PromptHistoryPage* m_historyPage = nullptr;
    OutputViewerPage* m_outputPage = nullptr;
    WorkflowEditPage* m_workflowPage = nullptr;
    WorkflowInputCache* m_inputCache = nullptr;
    EntryViewerPage* m_viewerPage = nullptr;
    DatasetHelpersPage* m_datasetPage = nullptr;

    // Final-image bookkeeping. A run arms m_pendingFinalLoad once it is
    // really sampling; the two skips disarm it when the run was cancelled
    // rather than finished.
    int m_lastQueueCount = 0;
    bool m_pendingFinalLoad = false;
    bool m_skipNextFinalLoad = false;
    bool m_skipPendingClearLoad = false;

    // Bumped when a prompt starts sampling. The delayed final load carries
    // the value it was armed with and drops out if a newer run has begun.
    int m_sampleGeneration = 0;

    // Built with the window, but it only scans directories: no ONNX session
    // is created until a model is actually asked for.
    std::unique_ptr<TaggerLibrary> m_taggers;

    WindowChrome* m_chrome = nullptr;
    NavBar* m_nav = nullptr;
    QStackedWidget* m_pages = nullptr;
    StatusBar* m_status = nullptr;
};

} // namespace tc
