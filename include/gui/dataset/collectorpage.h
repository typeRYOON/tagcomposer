#pragma once
#include <QWidget>

class QLineEdit;
class QPushButton;
class QSpinBox;
class QSlider;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QListWidget;

namespace utils {
struct AppSettings;
}
namespace core {
class DownloadWatcher;
}

namespace gui {

// Watches a "downloads" folder and routes new images into a named collection
// folder under data/collections/<name>/. Uses a 64-bit pHash + Hamming
// distance threshold to skip near-duplicates (sent to the recycle bin).
class CollectorPage : public QWidget {
    Q_OBJECT
public:
    explicit CollectorPage(utils::AppSettings* settings = nullptr, QWidget* parent = nullptr);
    ~CollectorPage() override;

    // AppMainWindow calls this on close so the watcher is never running
    // unattended after the GUI is gone.
    void stopWatcher();

signals:
    // "Send to Auto-tagger" - DatasetHelpersPage routes the active
    // collection's folder into AutoTagPage and switches tabs.
    void sendToAutoTaggerRequested(const QString& folder);

private:
    utils::AppSettings* m_settings = nullptr;
    core::DownloadWatcher* m_watcher = nullptr;

    // ── Left panel (controls) ───────────────────────────────────────────────
    QLineEdit* m_watchEdit = nullptr;
    QPushButton* m_watchBrowseBtn = nullptr;
    QComboBox* m_collectionBox = nullptr;
    QPushButton* m_newCollectionBtn = nullptr;
    QSlider* m_thresholdSlider = nullptr;
    QLabel* m_thresholdValue = nullptr;
    QSpinBox* m_pollSpin = nullptr;
    QPushButton* m_startStopBtn = nullptr;
    QPushButton* m_openFolderBtn = nullptr;
    QPushButton* m_rebuildBtn = nullptr;
    QPushButton* m_sendToTaggerBtn = nullptr;

    // ── Middle panel (activity) ─────────────────────────────────────────────
    QLabel* m_activeDot = nullptr; // green when running, grey idle
    QLabel* m_collectedLbl = nullptr;
    QLabel* m_skippedLbl = nullptr;
    QPlainTextEdit* m_log = nullptr;

    // ── Right panel (recent thumbs) ─────────────────────────────────────────
    // Newest-first thumbnail grid of images the watcher just moved into the
    // collection. Capped at a fixed count; oldest entries fall off the bottom.
    QListWidget* m_recentList = nullptr;
    QLabel* m_recentEmpty = nullptr;

    void refreshCollections();
    void onStartStop();
    void onNewCollection();
    void onRebuildIndex();
    void persistSettings();
    void setRunningUi(bool running);
    QString currentCollectionDir() const;

    // Loads `imagePath` on a worker thread, then prepends a thumbnail to the
    // recent panel on the GUI thread. Trims the list to the cap once added.
    void addRecentThumb(const QString& imagePath);
};

} // namespace gui
