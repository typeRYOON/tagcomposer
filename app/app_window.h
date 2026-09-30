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

// The shell: frameless chrome, navbar, page stack and status bar.
class AppWindow : public QMainWindow {
    Q_OBJECT

public:
    // dataDir holds system/ and entry/.
    explicit AppWindow(const QString& dataDir, QWidget* parent = nullptr);

    // Out of line for the unique_ptr to an incomplete type.
    ~AppWindow() override;

protected:
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    QWidget* buildPage(int index);
    void updateTitle();
    // Loads the finished image from disk; ComfyUI only streams sampler previews.
    void loadFinalPreview();

    // Shared by the composer's and the cluster page's quick-add menus.
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

    // Final-image loading: armed once a run samples, skipped after a cancel.
    int m_lastQueueCount = 0;
    bool m_pendingFinalLoad = false;
    bool m_skipNextFinalLoad = false;
    bool m_skipPendingClearLoad = false;

    // Bumped per sampling run so a stale delayed load can bail.
    int m_sampleGeneration = 0;

    // Only scans directories; sessions load on demand.
    std::unique_ptr<TaggerLibrary> m_taggers;

    WindowChrome* m_chrome = nullptr;
    NavBar* m_nav = nullptr;
    QStackedWidget* m_pages = nullptr;
    StatusBar* m_status = nullptr;
};

} // namespace tc
