#pragma once
#include <QLabel>
#include <QPixmap>
#include <QString>
#include <QWidget>

class QCloseEvent;
class QFileSystemWatcher;
class QImage;
class QKeyEvent;
class QTimer;
template <typename T>
class QFutureWatcher;

namespace tc {

class MovablePreviewLabel;
class StatusBar;
class WindowChrome;

// A label that keeps its source pixmap and rescales on every resize.
class ScaledImageLabel : public QLabel {
    Q_OBJECT

public:
    explicit ScaledImageLabel(QWidget* parent = nullptr);

    void setSourcePixmap(const QPixmap& pixmap);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateScaled();

    QPixmap m_source;
};

// The preview in its own top-level window, parented to the composer so Qt
// cleans it up. It forwards the run, interrupt and clear shortcuts back to
// the composer when it is the focused window.
class PreviewPopoutWindow : public QWidget {
    Q_OBJECT

public:
    explicit PreviewPopoutWindow(QWidget* parent = nullptr);

    void setImage(const QPixmap& image);
    void setOutputFolder(const QString& folder);
    void setTempFolder(const QString& folder);

    // Mirrors the main window's status bar: same widgets, same fades.
    void setProgress(int step, int total);
    void setActiveCount(int count);

signals:
    void runRequested();
    void interruptRequested();
    void clearPendingRequested();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void loadNewestTempImage();

    ScaledImageLabel* m_imageLabel = nullptr;
    MovablePreviewLabel* m_tempLabel = nullptr;
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer* m_debounce = nullptr;
    QFutureWatcher<QImage>* m_loadWatcher = nullptr;
    QString m_tempFolder;
    QString m_lastTempPath;
    bool m_closing = false;

    WindowChrome* m_chrome = nullptr;
    StatusBar* m_statusBar = nullptr;
};

} // namespace tc
