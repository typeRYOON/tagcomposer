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

// Frameless chrome for a top-level widget: titlebar, border and edge resize.
// The host sets FramelessWindowHint, puts frame() in its layout, lays content
// into body() and forwards changeEvent to onWindowStateChanged().
class WindowChrome : public QObject {
    Q_OBJECT

public:
    struct Options {
        bool showMin = true;
        bool showMax = true;
        bool showClose = true;

        // For exec()'d dialogs, whose modal loop breaks the implicit mouse grab.
        bool modalGrab = false;
    };

    explicit WindowChrome(QWidget* host);
    WindowChrome(QWidget* host, Options options);

    QWidget* frame() const;
    QWidget* body() const;
    TitleBar* titleBar() const;

    // Hides the chrome when fullscreen or maximized.
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

    // Polls the buttons during a drag in case the release never arrives.
    QTimer* m_dragGuard = nullptr;
};

} // namespace tc
