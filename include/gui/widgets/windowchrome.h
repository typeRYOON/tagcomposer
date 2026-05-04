#pragma once
#include <QObject>
#include <QPoint>
#include <QRect>
#include <Qt>

class QEvent;
class QWidget;

namespace gui {

class TitleBar;

// Reusable frameless-window chrome: titlebar, cosmetic border, and outline-
// style edge resize. Composes into any QWidget-derived host (QMainWindow,
// QDialog, top-level QWidget) - the host owns the WindowChrome instance and
// delegates chrome work to it.
//
// Wiring (host responsibilities):
//   1. setWindowFlags(... | Qt::FramelessWindowHint)
//   2. m_chrome = new WindowChrome(this, opts);
//   3. Insert m_chrome->frame() into the host's layout
//      (or setCentralWidget() for QMainWindow).
//   4. Layout content into m_chrome->bodyWidget().
//   5. Forward changeEvent → m_chrome->onWindowStateChanged().
//
// The chrome's QObject parent is the host, so it's destroyed automatically.
class WindowChrome : public QObject {
    Q_OBJECT
public:
    struct Options {
        bool showMin = true;
        bool showMax = true;
        bool showClose = true;
        // Set when the host is a QDialog using exec(). The application-modal
        // event loop disrupts Qt's implicit mouse grab during the resize
        // drag, so we install an explicit grabMouse() in beginResizeDrag.
        // Non-modal hosts (top-level QWidget / QMainWindow) don't need this.
        bool modalGrab = false;
    };

    WindowChrome(QWidget* host, Options opt = {});

    // Add this to the host's layout (or setCentralWidget for QMainWindow).
    QWidget* frame() const
    {
        return m_frame;
    }
    // The host should layout its content into this.
    QWidget* bodyWidget() const
    {
        return m_body;
    }
    TitleBar* titleBar() const
    {
        return m_titleBar;
    }

    // Hosts forward window state changes here so the chrome adapts to
    // fullscreen / maximize: titlebar hidden in fullscreen, no cosmetic
    // border in either, resize ring disabled in either.
    void onWindowStateChanged();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void beginResizeDrag(Qt::Edges edges, const QPoint& globalStart);
    void updateResizeOutline(const QPoint& globalNow);
    void endResizeDrag(const QPoint& globalNow);
    QRect computeResizeGeometry(const QPoint& globalNow) const;

    QWidget* m_host = nullptr;
    QWidget* m_frame = nullptr;
    QWidget* m_body = nullptr;
    QWidget* m_resizeOverlay = nullptr;
    QWidget* m_resizeOutline = nullptr; // lazy
    TitleBar* m_titleBar = nullptr;
    Options m_opt;
    Qt::Edges m_dragEdges{};
    QRect m_dragStartGeo;
    QPoint m_dragStartGlobal;
};

} // namespace gui
