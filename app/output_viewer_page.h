#pragma once
#include <QString>
#include <QWidget>

class QFileSystemModel;
class QLabel;
class QListWidgetItem;
class QModelIndex;
class QPushButton;
class QSplitter;

namespace tc {

class OutputThumbList;
class OutputTreeView;

// Browses ComfyUI's output folder: a tree of folders and images on the left,
// a thumbnail grid on the right for whatever folder the tree's current item
// belongs to. Clicking an image opens it in the system viewer; a folder in
// the right pane navigates the tree to it.
class OutputViewerPage : public QWidget {
    Q_OBJECT

public:
    explicit OutputViewerPage(QWidget* parent = nullptr);

    // The raw pattern from settings, which may carry a {yyyy-MM-dd} suffix.
    // Everything from the first brace on is stripped to find the real
    // on-disk root that anchors the tree.
    void setOutputFolder(const QString& folderPattern);

private slots:
    void onTreeCurrentChanged(const QModelIndex& current, const QModelIndex& previous);
    void onThumbActivated(QListWidgetItem* item);
    void onTreeActivated(const QModelIndex& index);
    void focusThumbForImage(const QString& imagePath);

private:
    void populateThumbsForDir(const QString& dirPath);
    void selectInTree(const QString& path);
    static void openInSystemViewer(const QString& path);

    // The model may not have realised the root's children when the folder is
    // first set, so navigating straight to today's dated subfolder can fail.
    // The wanted path waits here and is retried on every directoryLoaded.
    void navigateToPendingIfReady();

    QString m_pattern;
    QString m_root;

    // Which folder the right pane is showing. Compared against a new
    // selection so arrowing between siblings of one parent does not rebuild
    // the grid for no reason.
    QString m_currentThumbDir;
    QString m_pendingNavTo;

    QFileSystemModel* m_model = nullptr;
    OutputTreeView* m_tree = nullptr;
    OutputThumbList* m_thumbs = nullptr;
    QSplitter* m_splitter = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_treeSubtitle = nullptr;  // the current root path
    QLabel* m_thumbSubtitle = nullptr; // the current folder and its counts
    QPushButton* m_openFolderBtn = nullptr;
};

} // namespace tc
