#pragma once
#include <QPixmap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QSyntaxHighlighter>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTimer;

namespace tc {

class DanbooruIndex;
struct Settings;
class TagSearchBar;

// Paints each comma-separated token of a pattern in its own colour, so two
// matches on the same line stay distinguishable. The pattern is set whole
// ("red, black, hair") and split here.
class TagSearchHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit TagSearchHighlighter(QTextDocument* parent = nullptr);

    void setPattern(const QString& pattern);

protected:
    void highlightBlock(const QString& text) override;

private:
    QStringList m_patterns;
};

// Edits the .txt sidecars beside a folder of images, one image at a time.
// Nothing here loads a model or infers anything: it is file I/O on
// `<folder>/<basename>.txt`, debounced and saved as you type.
class TagEditorPage : public QWidget {
    Q_OBJECT

public:
    explicit TagEditorPage(Settings& settings, QWidget* parent = nullptr);
    ~TagEditorPage() override;

    // The handoff from another tab, which has already picked the folder.
    void setInputFolder(const QString& folder);

    void setDanbooruIndex(const DanbooruIndex* index);

signals:
    void sendToBatchEditRequested(const QString& folder);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void rescan();
    void jumpTo(int index);
    void rescalePreview();
    void scheduleSave();
    void saveNow();
    void persistSettings();
    void rebuildActiveTags();
    void appendTag(const QString& tag);
    void deleteCurrent();
    void updateNavButtons();

    Settings* m_settings = nullptr;
    const DanbooruIndex* m_danbooru = nullptr;

    // The folder scan decodes an image, which startup should not pay for on
    // a page that may never be opened. Deferred to the first show.
    bool m_pendingScan = false;

    QLineEdit* m_folderEdit = nullptr;
    QCheckBox* m_recursive = nullptr;
    QLabel* m_position = nullptr;
    QPushButton* m_first = nullptr;
    QPushButton* m_back10 = nullptr;
    QPushButton* m_back = nullptr;
    QPushButton* m_forward = nullptr;
    QPushButton* m_forward10 = nullptr;
    QPushButton* m_last = nullptr;
    QPushButton* m_delete = nullptr;
    QPushButton* m_sendToBatch = nullptr;

    QLabel* m_preview = nullptr;
    QLabel* m_imageName = nullptr;

    // Native resolution, kept so a window resize re-scales without going back
    // to disk for another decode.
    QPixmap m_previewSource;

    TagSearchBar* m_tagSearch = nullptr;
    QPlainTextEdit* m_tagEdit = nullptr;
    QLineEdit* m_highlightEdit = nullptr;
    QLabel* m_status = nullptr;
    TagSearchHighlighter* m_highlighter = nullptr;

    QStringList m_images; // absolute, sorted
    int m_index = -1;
    QString m_imagePath;
    QString m_txtPath;
    QTimer* m_saveTimer = nullptr;

    // What the search bar checks before offering a tag.
    QSet<QString> m_activeTags;
};

} // namespace tc
