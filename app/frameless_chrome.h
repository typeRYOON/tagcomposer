#pragma once
#include <QColor>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPoint>
#include <QSize>
#include <QWidget>
#include <Qt>

namespace tc::chrome {

// Visible cosmetic border: the frame's layout margin.
constexpr int kResizeBorder = 2;

// Resize hit area along each edge.
constexpr int kResizeHit = 6;

inline Qt::Edges edgesAt(const QPoint& pos, const QSize& size)
{
    Qt::Edges edges;
    if (pos.x() <= kResizeHit)
        edges |= Qt::LeftEdge;
    else if (pos.x() >= size.width() - kResizeHit)
        edges |= Qt::RightEdge;

    if (pos.y() <= kResizeHit)
        edges |= Qt::TopEdge;
    else if (pos.y() >= size.height() - kResizeHit)
        edges |= Qt::BottomEdge;

    return edges;
}

inline Qt::CursorShape cursorForEdges(Qt::Edges edges)
{
    switch (int(edges)) {
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

// Outline previewing a resize; the window resizes on release.
class ResizeOutline : public QWidget {
public:
    ResizeOutline()
        : QWidget(nullptr, Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint
                               | Qt::WindowDoesNotAcceptFocus)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        QPen pen(QColor(220, 220, 220, 220));
        pen.setWidth(2);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect().adjusted(1, 1, -1, -1));
    }
};

} // namespace tc::chrome
