#pragma once
#include <QHash>
#include <QList>
#include <QPixmap>
#include <QPointer>
#include <QString>
#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;
class QScrollArea;
class QSplitter;
class QVBoxLayout;

namespace tc {

class ComfyClient;
class ComposerPage;
class EntryStore;
class PromptHistory;

// The session's queued prompts, newest first, with re-queue (same seed), save
// as state, and restore to the composer.
class PromptHistoryPage : public QWidget {
    Q_OBJECT

public:
    PromptHistoryPage(PromptHistory& history, EntryStore& entries, ComposerPage& composer,
                      ComfyClient& comfy, QWidget* parent = nullptr);

signals:
    void statusMessage(const QString& message);
    void switchToComposerRequested();

    // From an entry tile click.
    void openEntryRequested(const QString& uuid);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuildList();
    void onSelectionChanged();
    void rebuildDetails();
    void updateActionEnabled();

    void doRequeue();
    void doSaveState();
    void doRestore();

    // The history index stored in the selected row's UserRole.
    int selectedRecordIndex() const;

    // Thumbnails decode on a worker pool, cached by "uuid|fileName".
    QWidget* buildEntryTile(const QString& uuid, const QString& imageFile, qsizetype tagCount,
                            QWidget* parent);
    void requestThumb(const QString& key, const QString& absolutePath);

    // Reflowed on viewport resize.
    int computeTileColumns() const;
    void layoutTileGrid(int columns);

    PromptHistory* m_history = nullptr;
    EntryStore* m_entries = nullptr;
    ComposerPage* m_composer = nullptr;
    ComfyClient* m_comfy = nullptr;

    QListWidget* m_list = nullptr;
    QPushButton* m_clearAllBtn = nullptr;
    QLabel* m_emptyLabel = nullptr;
    QSplitter* m_splitter = nullptr;

    QScrollArea* m_detailsScroll = nullptr;
    QWidget* m_detailsHost = nullptr;
    QVBoxLayout* m_detailsLayout = nullptr;

    QPushButton* m_requeueBtn = nullptr;
    QPushButton* m_saveStateBtn = nullptr;
    QPushButton* m_restoreBtn = nullptr;

    QHash<QString, QPixmap> m_thumbCache;
    QHash<QString, QList<QPointer<QLabel>>> m_pendingThumbs;

    QPointer<QWidget> m_tileGridHost;
    int m_tileGridColumns = 0;
};

} // namespace tc
