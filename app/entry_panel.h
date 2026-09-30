#pragma once
#include <QColor>
#include <QSet>
#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QDoubleSpinBox;
class QScrollArea;
class QTimer;
class QVBoxLayout;

namespace tc {

class ComfyClient;
class ComposerStore;
class DanbooruIndex;
class EntryStore;
class ImageDropper;
class LoraDropZone;
class TagSearchBar;
struct Settings;

// Detail side of the entry viewer: image with paging, title, comment, and the
// tag list for the current image.
//
// It holds a uuid, never an Entry*, and re-reads through the store on every
// refresh. Every edit goes through EntryStore, which writes through, so there
// is nothing to save here and nothing to keep in sync.
class EntryPanel : public QWidget {
    Q_OBJECT

public:
    EntryPanel(EntryStore& store, ComposerStore& composer, const Settings& settings,
               ComfyClient& comfy, QWidget* parent = nullptr);

    void setEntry(const QString& uuid);
    QString entry() const;

    // Called after a click in the grid so typing can start without another.
    void focusTagInput();

    // The same thing the Composer button does. Enter on a grid tile uses it,
    // so both routes go through one place.
    void toggleComposerPush();

    // The LoRA roots come from settings, which are read after this is built.
    void applySettings();

    // Lands after the library loads, so the rows are rebuilt to pick up the
    // category colours they were drawn without.
    void setDanbooruIndex(const DanbooruIndex* index);

signals:
    // A grid navigation key pressed anywhere in the panel: the page hands
    // focus back to the tile grid and replays it there.
    void gridNavRequested(int key);

    void statusMessage(const QString& message);

    // The panel deleted the entry it was showing.
    void entryDeleted(const QString& uuid);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refresh();
    void showImagePage(int index);
    void rebuildTagList();
    void updateComposerToggle();
    void refreshLoraSection();
    void assignLoraFile(const QString& sourcePath);
    void showLoraMenu(const QPoint& globalPos);
    QString loraAbsolutePath() const;
    QString currentImageFile() const;
    QWidget* makeTagRow(const QString& tag);
    QColor tagColour(const QString& tag) const;
    void clearTagRows();

    // The toggle pushes this entry's tags into the composer, so the panel
    // depends on the composer store. That is what the button does.
    EntryStore* m_store = nullptr;
    ComposerStore* m_composerStore = nullptr;
    const Settings* m_settings = nullptr;
    ComfyClient* m_comfy = nullptr;
    QString m_uuid;
    int m_imageIndex = 0;

    ImageDropper* m_image = nullptr;
    QLineEdit* m_title = nullptr;
    QPlainTextEdit* m_comment = nullptr;
    QTimer* m_commentTimer = nullptr;

    QPushButton* m_prev = nullptr;
    QPushButton* m_next = nullptr;
    QPushButton* m_addImage = nullptr;
    QPushButton* m_removeImage = nullptr;
    QLabel* m_pageLabel = nullptr;

    QPushButton* m_composer = nullptr;
    QPushButton* m_copy = nullptr;
    QPushButton* m_delete = nullptr;

    // ---- LoRA
    LoraDropZone* m_loraDrop = nullptr;
    QPushButton* m_loraClear = nullptr;
    QPushButton* m_loraHash = nullptr;
    QDoubleSpinBox* m_loraModelStrength = nullptr;
    QDoubleSpinBox* m_loraClipStrength = nullptr;
    QTimer* m_loraTimer = nullptr;

    TagSearchBar* m_tagSearch = nullptr;
    const DanbooruIndex* m_danbooru = nullptr;

    // What the search bar checks before offering a tag; kept in step with the
    // rows in rebuildTagList.
    QSet<QString> m_activeTags;
    QScrollArea* m_tagScroll = nullptr;
    QVBoxLayout* m_tagRows = nullptr;
};

} // namespace tc
