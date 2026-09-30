#pragma once
#include <QString>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;

namespace tc {

class DownloadWatcher;
struct Settings;

// Watches a downloads folder and files new images into a named collection;
// near-duplicates (by perceptual hash) go to the recycle bin.
class CollectorPage : public QWidget {
    Q_OBJECT

public:
    CollectorPage(Settings& settings, const QString& collectionsRoot, QWidget* parent = nullptr);
    ~CollectorPage() override;

    // Called on close.
    void stopWatcher();

signals:
    void sendToAutoTaggerRequested(const QString& folder);

private:
    void refreshCollections();
    void startOrStop();
    void newCollection();
    void rebuildIndex();
    void persistSettings();
    void setRunningUi(bool running);
    QString currentCollectionDir() const;

    // Decodes off the GUI thread, then prepends to the recent grid.
    void addRecentThumb(const QString& imagePath);

    Settings* m_settings = nullptr;
    QString m_collectionsRoot;
    DownloadWatcher* m_watcher = nullptr;

    QLineEdit* m_watchEdit = nullptr;
    QPushButton* m_watchBrowse = nullptr;
    QComboBox* m_collections = nullptr;
    QPushButton* m_newCollection = nullptr;
    QSlider* m_threshold = nullptr;
    QLabel* m_thresholdValue = nullptr;
    QSpinBox* m_poll = nullptr;
    QPushButton* m_startStop = nullptr;
    QPushButton* m_openFolder = nullptr;
    QPushButton* m_rebuild = nullptr;
    QPushButton* m_sendToTagger = nullptr;

    QLabel* m_runState = nullptr;
    QLabel* m_collected = nullptr;
    QLabel* m_skipped = nullptr;
    QPlainTextEdit* m_log = nullptr;

    // Newest first, capped.
    QListWidget* m_recent = nullptr;
    QLabel* m_recentEmpty = nullptr;
};

} // namespace tc
