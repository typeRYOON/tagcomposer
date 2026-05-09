#pragma once
#include <QString>
#include <QWidget>

class QFileSystemWatcher;
class QKeyEvent;
class QTimer;
template <typename T> class QFutureWatcher;
class QImage;
class QPixmap;

namespace gui {

class ScaledImageLabel;
class ClickableLabel;
class StatusBar;
class WindowChrome;

// Separate top-level window (Qt::Window) parented to PromptComposerPage so Qt
// handles cleanup. Has Q_OBJECT so it can forward keyboard-shortcut intents
// (run / interrupt / clear) up to the composer when this window is focused.
class PreviewPopoutWindow : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPopoutWindow(QWidget* parent = nullptr);

    void setImage(const QPixmap& pix);
    void setOutputFolder(const QString& folder);
    void setTempFolder(const QString& folder);
    // Mirrors AppMainWindow's bottom StatusBar - same widgets, same fades.
    void setProgress(int step, int total);
    void setActiveCount(int count);

signals:
    void runRequested();
    void interruptRequested();
    void clearPendingRequested();

protected:
    void resizeEvent(QResizeEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void closeEvent(QCloseEvent* e) override;
    void changeEvent(QEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    void loadNewestTempImage();

    ScaledImageLabel* m_imageLabel;
    ClickableLabel* m_tempLabel;
    QFileSystemWatcher* m_watcher;
    QTimer* m_debounce;
    QFutureWatcher<QImage>* m_loadWatcher;
    QString m_tempFolder;
    QString m_lastTempPath;
    bool m_isClosing{false};

    WindowChrome* m_chrome = nullptr;
    StatusBar* m_statusBar = nullptr;
};

} // namespace gui
