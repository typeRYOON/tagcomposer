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

// Detail side of the entry viewer: image paging, title, comment and tags.
// Holds a uuid and edits through EntryStore.
class EntryPanel : public QWidget {
    Q_OBJECT

public:
    EntryPanel(EntryStore& store, ComposerStore& composer, const Settings& settings,
               ComfyClient& comfy, QWidget* parent = nullptr);

    void setEntry(const QString& uuid);
    QString entry() const;

    void focusTagInput();

    // Same as the Composer button.
    void toggleComposerPush();

    // Re-reads the LoRA roots from settings.
    void applySettings();

    // Rebuilds the rows with category colors.
    void setDanbooruIndex(const DanbooruIndex* index);

signals:
    // A grid navigation key pressed in the panel.
    void gridNavRequested(int key);

    void statusMessage(const QString& message);

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

    // The current image's tags, so the search bar can skip them.
    QSet<QString> m_activeTags;
    QScrollArea* m_tagScroll = nullptr;
    QVBoxLayout* m_tagRows = nullptr;
};

} // namespace tc
