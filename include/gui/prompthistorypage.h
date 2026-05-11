#pragma once
#include <QHash>
#include <QList>
#include <QPixmap>
#include <QPointer>
#include <QString>
#include <QWidget>

class QListWidget;
class QListWidgetItem;
class QPushButton;
class QLabel;
class QScrollArea;
class QVBoxLayout;
class QSplitter;

namespace core {
class PromptHistory;
class EntryModel;
class ComfyUiClient;
}

namespace gui {

class PromptComposerPage;

// Session-only log of every queuePrompt push. Left column lists records
// newest-first; right column shows the snapshot for the selected record
// with three per-row actions: re-queue (replays the exact JSON, same seed),
// save as state (forwards the snapshot into the composer's state manager),
// restore to composer (loads the snapshot back into PromptComposerPage and
// switches pages).
class PromptHistoryPage : public QWidget {
    Q_OBJECT
public:
    explicit PromptHistoryPage(core::PromptHistory* history, core::EntryModel* entryModel,
                               gui::PromptComposerPage* composer, core::ComfyUiClient* comfy,
                               QWidget* parent = nullptr);

signals:
    void statusMessageRequested(const QString& message);
    void switchToComposerRequested();
    // Active-entry tile click: jump to the entry viewer, drop its active
    // query, and select this entry.
    void openEntryRequested(int entryId);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    void rebuildList();
    void onSelectionChanged();
    void rebuildDetails();
    void updateActionEnabled();

    void doRequeue();
    void doSaveState();
    void doRestore();

    // Index into m_history->records() that the currently selected list row
    // refers to. The list row's Qt::UserRole carries this index, so list
    // mutations (prepend on new record, clear) stay sync'd.
    int selectedRecordIndex() const;

    core::PromptHistory* m_history;
    core::EntryModel* m_entryModel;
    gui::PromptComposerPage* m_composer;
    core::ComfyUiClient* m_comfy;

    QListWidget* m_listWidget = nullptr;
    QPushButton* m_clearAllBtn = nullptr;
    QLabel* m_emptyLabel = nullptr;
    QSplitter* m_splitter = nullptr;

    QScrollArea* m_detailsScroll = nullptr;
    QWidget* m_detailsHost = nullptr;
    QVBoxLayout* m_detailsLayout = nullptr;

    QPushButton* m_requeueBtn = nullptr;
    QPushButton* m_saveStateBtn = nullptr;
    QPushButton* m_restoreBtn = nullptr;

    // Thumbnail builder + cache. Sync loads blocked the UI for records with
    // many entries, so tile images are decoded + scaled on a worker pool and
    // pushed back to the QLabel when ready. Cache key: "uuid|imageFileName".
    QWidget* buildEntryTile(const QString& uuid, const QString& imageFileName,
                            int tagCount, QWidget* parent);
    void requestThumb(const QString& key, const QString& absPath);
    QHash<QString, QPixmap> m_thumbCache;
    QHash<QString, QList<QPointer<QLabel>>> m_pendingThumbs;

    // Active-entry tile grid lives inside m_detailsHost. Column count is
    // recomputed on every viewport resize (page resize + splitter drag) so
    // tiles fill the row as far as the available width permits.
    int computeTileCols() const;
    void layoutTileGrid(int cols);
    QPointer<QWidget> m_tileGridHost;
    int m_tileGridCols = 0;
};

} // namespace gui
