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

// The session log of every prompt pushed to ComfyUI. The list on the left is
// newest first; the pane on the right shows the selected record's snapshot
// with three actions: re-queue it verbatim (same seed), save the snapshot as
// a composer state, or restore it into the composer and switch pages.
class PromptHistoryPage : public QWidget {
    Q_OBJECT

public:
    PromptHistoryPage(PromptHistory& history, EntryStore& entries, ComposerPage& composer,
                      ComfyClient& comfy, QWidget* parent = nullptr);

signals:
    void statusMessage(const QString& message);
    void switchToComposerRequested();

    // A click on an active-entry tile: the shell opens the entry viewer and
    // selects that entry.
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

    // The index into the history that the selected row refers to. The row
    // carries it in UserRole, so a prepend or a clear cannot desync it.
    int selectedRecordIndex() const;

    // Tiles decode on a worker pool: a record with many entries used to block
    // the UI while every image was read synchronously. Keyed "uuid|fileName",
    // so the same image shared across records decodes once.
    QWidget* buildEntryTile(const QString& uuid, const QString& imageFile, qsizetype tagCount,
                            QWidget* parent);
    void requestThumb(const QString& key, const QString& absolutePath);

    // The tile grid reflows on every viewport resize, so a splitter drag
    // refills the row rather than leaving a ragged edge.
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
