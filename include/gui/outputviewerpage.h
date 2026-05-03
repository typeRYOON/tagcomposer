#pragma once
#include <QWidget>
#include <QString>

class QFileSystemModel;
class QListWidgetItem;
class QSplitter;
class QModelIndex;
class QLabel;

namespace gui {

class OutputTreeView;
class OutputThumbList;

// Browses ComfyUI's output folder. Tree on the left (folders + image files);
// thumb grid on the right populated from whatever folder the tree's current
// item belongs to. Click an image (in either pane) to open it with the OS's
// default image viewer; click/Enter on a folder in the right pane to navigate
// the tree to it.
class OutputViewerPage : public QWidget {
    Q_OBJECT
public:
    explicit OutputViewerPage(QWidget* parent = nullptr);

    // Folder pattern is the raw string from settings (may contain a
    // `{yyyy-MM-dd}`-style suffix). Anything from the first `{` onward is
    // stripped to find the real on-disk root that should anchor the tree.
    void setOutputFolder(const QString& folderPattern);

private slots:
    void onTreeCurrentChanged(const QModelIndex& current, const QModelIndex& previous);
    void onThumbActivated(QListWidgetItem* item);
    void onTreeActivated(const QModelIndex& index);
    void focusThumbForImage(const QString& imagePath);

private:
    void populateThumbsForDir(const QString& dirPath);
    void selectInTree(const QString& path);
    void openInSystemViewer(const QString& path);

    QString m_pattern;
    QString m_root;

    // Path of whatever folder the right pane is currently showing. Compared
    // against the new selection in onTreeCurrentChanged so we skip rebuilding
    // the thumb grid when the user arrows between siblings of the same parent.
    QString m_currentThumbDir;

    // When `setOutputFolder` is called the QFileSystemModel may not have
    // populated the children of the root yet, so an immediate attempt to
    // navigate to today's date subfolder can fail (the index isn't realised).
    // We stash the desired path here and retry whenever directoryLoaded fires.
    QString m_pendingNavTo;
    void navigateToPendingIfReady();

    QFileSystemModel* m_fsModel        = nullptr;
    OutputTreeView*   m_tree           = nullptr;
    OutputThumbList*  m_thumbs         = nullptr;
    QSplitter*        m_split          = nullptr;
    QLabel*           m_status         = nullptr;
    QLabel*           m_treeSubtitle   = nullptr;  // shows current root path
    QLabel*           m_thumbSubtitle  = nullptr;  // shows current dir + image count
};

} // namespace gui
