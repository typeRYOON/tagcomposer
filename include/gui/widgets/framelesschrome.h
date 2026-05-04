#pragma once
#include <QColor>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPoint>
#include <QSize>
#include <QWidget>
#include <Qt>

// Shared building blocks for our frameless windows (AppMainWindow, the
// preview popout, etc). Each window owns its own m_frame / m_resizeOverlay /
// drag state - but the geometric helpers and the outline preview widget
// don't vary between windows, so they live here.
namespace gui::framelesschrome {

// Width of the visible cosmetic border (m_frame's layout margin).
constexpr int kResizeBorder = 2;
// Width of the invisible resize hit-test ring. Mouse events on the outer
// kResizeHit pixels of the overlay are treated as edge grabs; this is also
// the threshold edgesAt() uses to decide which edge(s) to pull on.
constexpr int kResizeHit = 6;

// Returns which edges (if any) of `size`'s rect the cursor at `pos` is
// within kResizeHit pixels of. Used both for the hover-cursor lookup and
// for kicking off a drag on press.
inline Qt::Edges edgesAt(const QPoint& pos, const QSize& size)
{
    Qt::Edges e;
    if (pos.x() <= kResizeHit)
        e |= Qt::LeftEdge;
    else if (pos.x() >= size.width() - kResizeHit)
        e |= Qt::RightEdge;
    if (pos.y() <= kResizeHit)
        e |= Qt::TopEdge;
    else if (pos.y() >= size.height() - kResizeHit)
        e |= Qt::BottomEdge;
    return e;
}

inline Qt::CursorShape cursorForEdges(Qt::Edges e)
{
    switch (int(e)) {
    case int(Qt::TopEdge | Qt::LeftEdge):
    case int(Qt::BottomEdge | Qt::RightEdge):
        return Qt::SizeFDiagCursor;
    case int(Qt::TopEdge | Qt::RightEdge):
    case int(Qt::BottomEdge | Qt::LeftEdge):
        return Qt::SizeBDiagCursor;
    case int(Qt::TopEdge):
    case int(Qt::BottomEdge):
        return Qt::SizeVerCursor;
    case int(Qt::LeftEdge):
    case int(Qt::RightEdge):
        return Qt::SizeHorCursor;
    default:
        return Qt::ArrowCursor;
    }
}

// Top-level transparent widget that draws a 2px rectangle outline at its
// geometry. Used to preview the new window size during an outline-resize
// drag - the actual window is left alone until the mouse is released.
class ResizeOutline : public QWidget {
public:
    ResizeOutline()
        : QWidget(nullptr, Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint |
                               Qt::WindowDoesNotAcceptFocus)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        // Click-through; the captured widget on the host window receives
        // mouse events even when the cursor is over this overlay.
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        QPen pen(QColor(220, 220, 220, 220));
        pen.setWidth(2);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect().adjusted(1, 1, -1, -1));
    }
};

} // namespace gui::framelesschrome
