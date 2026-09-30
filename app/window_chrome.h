#pragma once
#include <QObject>
#include <QPoint>
#include <QRect>
#include <Qt>

class QEvent;
class QTimer;
class QWidget;

namespace tc {

class TitleBar;

// Frameless chrome for any top-level widget: titlebar, cosmetic border, and
// outline-style edge resize. The host owns it and delegates.
//
// Host responsibilities:
//   1. setWindowFlags(... | Qt::FramelessWindowHint)
//   2. auto* chrome = new WindowChrome(this, opts);
//   3. put chrome->frame() in the layout, or setCentralWidget for a QMainWindow
//   4. lay content into chrome->body()
//   5. forward changeEvent to chrome->onWindowStateChanged()
class WindowChrome : public QObject {
    Q_OBJECT

public:
    struct Options {
        bool showMin = true;
        bool showMax = true;
        bool showClose = true;

        // For a QDialog using exec(): the modal loop breaks Qt's implicit
        // mouse grab during a resize drag, so one is taken explicitly.
        bool modalGrab = false;
    };

    explicit WindowChrome(QWidget* host);
    WindowChrome(QWidget* host, Options options);

    QWidget* frame() const;
    QWidget* body() const;
    TitleBar* titleBar() const;

    // Titlebar hides in fullscreen, the border goes in fullscreen and
    // maximized, and the resize ring is off in both.
    void onWindowStateChanged();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void beginResizeDrag(Qt::Edges edges, const QPoint& globalStart);
    void updateResizeOutline(const QPoint& globalNow);
    void endResizeDrag(const QPoint& globalNow);
    QRect resizeGeometry(const QPoint& globalNow) const;

    QWidget* m_host = nullptr;
    QWidget* m_frame = nullptr;
    QWidget* m_body = nullptr;
    QWidget* m_resizeOverlay = nullptr;
    QWidget* m_resizeOutline = nullptr; // built on first drag
    TitleBar* m_titleBar = nullptr;
    Options m_options;

    Qt::Edges m_dragEdges{};
    QRect m_dragStartGeometry;
    QPoint m_dragStartGlobal;

    // Polls the mouse buttons while a drag is live. If the implicit grab is
    // broken mid-drag the release never arrives, and without this the outline
    // and override cursor would stay stuck on screen.
    QTimer* m_dragGuard = nullptr;
};

} // namespace tc
