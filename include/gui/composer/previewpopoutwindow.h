#pragma once
#include <QPoint>
#include <QRect>
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
class TitleBar;

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
    void keyPressEvent(QKeyEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void loadNewestTempImage();

    void  beginResizeDrag(Qt::Edges edges, const QPoint& globalStart);
    void  updateResizeOutline(const QPoint& globalNow);
    void  endResizeDrag(const QPoint& globalNow);
    QRect computeResizeGeometry(const QPoint& globalNow) const;

    ScaledImageLabel*       m_imageLabel;
    ClickableLabel*         m_tempLabel;
    QFileSystemWatcher*     m_watcher;
    QTimer*                 m_debounce;
    QFutureWatcher<QImage>* m_loadWatcher;
    QString                 m_tempFolder;
    QString                 m_lastTempPath;
    bool                    m_isClosing{ false };

    // Frameless chrome (mirrors AppMainWindow's setup).
    TitleBar*               m_titleBar      = nullptr;
    QWidget*                m_frame         = nullptr;
    QWidget*                m_resizeOverlay = nullptr;
    QWidget*                m_resizeOutline = nullptr;  // lazy

    Qt::Edges               m_dragEdges{};
    QRect                   m_dragStartGeo;
    QPoint                  m_dragStartGlobal;
};

} // namespace gui
