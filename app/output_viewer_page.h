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

// Browses ComfyUI's output folder: a folder tree and a thumbnail grid.
class OutputViewerPage : public QWidget {
    Q_OBJECT

public:
    explicit OutputViewerPage(QWidget* parent = nullptr);

    // The settings pattern; anything from the first '{' on is dropped for the root.
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

    // Retried on directoryLoaded until the model has the target path.
    void navigateToPendingIfReady();

    QString m_pattern;
    QString m_root;

    // The folder the grid shows; unchanged selections skip the rebuild.
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
