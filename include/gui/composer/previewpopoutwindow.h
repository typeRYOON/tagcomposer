#pragma once
#include <QWidget>
#include <QString>

class QFileSystemWatcher;
class QTimer;
template <typename T> class QFutureWatcher;
class QImage;
class QPixmap;

namespace gui {

class ScaledImageLabel;
class ClickableLabel;

// Separate top-level window (Qt::Window) parented to PromptComposerPage so Qt
// handles cleanup. Has Q_OBJECT so it can forward keyboard-shortcut intents
// (run / interrupt / clear) up to the composer when this window is focused.
class PreviewPopoutWindow : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPopoutWindow(QWidget* parent = nullptr);

    void setImage(const QPixmap& pix);
    void setOutputFolder(const QString&) {}
    void setTempFolder(const QString& folder);

signals:
    void runRequested();
    void interruptRequested();
    void clearPendingRequested();

protected:
    void resizeEvent(QResizeEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void closeEvent(QCloseEvent* e) override;
    void changeEvent(QEvent* e) override;

private:
    void loadNewestTempImage();

    ScaledImageLabel*       m_imageLabel;
    ClickableLabel*         m_tempLabel;
    QFileSystemWatcher*     m_watcher;
    QTimer*                 m_debounce;
    QFutureWatcher<QImage>* m_loadWatcher;
    QString                 m_tempFolder;
    QString                 m_lastTempPath;
    bool                    m_isFullScreen{ false };
    bool                    m_isClosing{ false };
};

} // namespace gui
