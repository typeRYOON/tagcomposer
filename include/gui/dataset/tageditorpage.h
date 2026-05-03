#pragma once
#include <QWidget>
#include <QSyntaxHighlighter>
#include <QHash>
#include <QPixmap>
#include <QSet>
#include <QString>
#include <QStringList>

class QLineEdit;
class QPushButton;
class QCheckBox;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QLabel;
class QTimer;

namespace utils { struct AppSettings; }
namespace core  { class DanbooruIndex; }

namespace gui {

class TagSearchBar;

// Highlighter that paints multiple comma-separated search tokens, each with
// its own colour from a small palette so the user can tell distinct matches
// apart. Set the pattern as a single string ("red, black, hair") and the
// highlighter splits internally.
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

// Standalone editor for the .txt sidecars produced by AutoTagPage. No
// inference, no model loading — just file I/O on `<folder>/<basename>.txt`
// next to each image.
//
// Workflow:
//   1. Pick a folder (recursive optional) — populates the image list.
//   2. Click an image — loads the image preview + the .txt contents.
//   3. Edit text — debounced auto-save back to the .txt file.
//   4. Search box — substring highlighting in the editor pane.
class TagEditorPage : public QWidget {
    Q_OBJECT
public:
    TagEditorPage(core::DanbooruIndex* danbooruIndex,
                  utils::AppSettings*  settings,
                  QWidget*             parent = nullptr);
    ~TagEditorPage() override;

    // Public entry point used by the AutoTag → Tag Editor handoff.
    void setInputFolder(const QString& folder);

    // Late-binding hook for AppMainWindow — DanbooruIndex loads asynchronously
    // at startup; this page may be constructed before it's ready.
    void setDanbooruIndex(core::DanbooruIndex* index);

signals:
    // "Send to Batch Edit" — DatasetHelpersPage routes the folder into
    // BatchEditPage and switches tabs.
    void sendToBatchEditRequested(const QString& folder);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    core::DanbooruIndex* m_danbooruIndex = nullptr;
    utils::AppSettings*  m_settings      = nullptr;

    // ── Left column (folder + navigation) ───────────────────────────────────
    QLineEdit*    m_folderEdit     = nullptr;
    QPushButton*  m_browseBtn      = nullptr;
    QCheckBox*    m_recursiveCheck = nullptr;
    QLabel*       m_positionLbl    = nullptr;
    QPushButton*  m_navFirstBtn    = nullptr;
    QPushButton*  m_navPrev10Btn   = nullptr;
    QPushButton*  m_navPrevBtn     = nullptr;
    QPushButton*  m_navNextBtn     = nullptr;
    QPushButton*  m_navNext10Btn   = nullptr;
    QPushButton*  m_navLastBtn     = nullptr;
    QPushButton*  m_deleteBtn      = nullptr;
    QPushButton*  m_sendToBatchBtn = nullptr;

    // ── Middle column (large clickable preview) ─────────────────────────────
    QLabel*       m_focusImage     = nullptr;
    QLabel*       m_imageNameLbl   = nullptr;  // sits in the IMAGE section header
    // Source pixmap at native resolution. The label's displayed pixmap is
    // scaled from this on every load + every resizeEvent so the preview
    // grows/shrinks with the window.
    QPixmap       m_focusPixmapSrc;

    // ── Right column (search bar + editor + highlight) ──────────────────────
    TagSearchBar*    m_tagSearchBar = nullptr;
    QPlainTextEdit*  m_tagEdit      = nullptr;
    QLineEdit*       m_highlightEdit= nullptr;
    QLabel*          m_editStatus   = nullptr;

    // ── State ───────────────────────────────────────────────────────────────
    QStringList           m_images;          // absolute paths, alphabetical
    int                   m_currentIndex = -1;
    QString               m_currentImagePath;     // absolute (== m_images[m_currentIndex])
    QString               m_currentTxtPath;       // absolute
    QTimer*               m_saveTimer    = nullptr;
    TagSearchHighlighter* m_highlighter  = nullptr;
    // Active tags in canonical (underscore) form, derived from the editor
    // text. Passed to the TagSearchBar so it can flag duplicates.
    QSet<QString>         m_activeCanonical;

    void rescan();
    void jumpTo(int newIndex);
    void rescalePreview();
    void scheduleSave();
    void saveNow();
    void persistSettings();
    void rebuildActiveTags();
    void onSearchBarTagAdded(const QString& canonical);
    void onDeleteClicked();
    void updateNavigationButtons();
};

} // namespace gui
