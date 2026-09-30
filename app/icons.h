#pragma once
#include <QAbstractButton>
#include <QColor>
#include <QEvent>
#include <QIcon>
#include <QObject>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QRectF>
#include <QSize>
#include <cmath>
#include <utility>

// Header-only icon factory. Paints small monochrome icons into QPixmaps so
// the dark theme stays consistent without shipping a PNG per glyph.
namespace tc::icons {

namespace detail {

// Painter primed for line-art: rounded caps/joins, no fill, antialiased.
inline void primeLine(QPainter& p, int px, QColor color, qreal strokeFactor)
{
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(color);
    pen.setWidthF(px * strokeFactor);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
}

} // namespace detail

// "Open in editor": NE arrow leaving a half-frame, like a browser external-link glyph.
inline QIcon openExternal(int px = 16, QColor color = QColor(0x9a, 0x9a, 0x9a))
{
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    detail::primeLine(p, px, color, 1.0 / 9.0);

    const qreal m = px * 0.20;
    const qreal cut = px * 0.45;

    // Bracket opening at the upper-right corner.
    QPainterPath frame;
    frame.moveTo(px - m, px - cut);
    frame.lineTo(px - m, px - m);
    frame.lineTo(m, px - m);
    frame.lineTo(m, m);
    frame.lineTo(px - cut, m);
    p.drawPath(frame);

    const QPointF tail(px * 0.42, px * 0.58);
    const QPointF tip(px * 0.86, px * 0.14);
    p.drawLine(tail, tip);
    p.drawLine(tip, tip + QPointF(-px * 0.26, 0));
    p.drawLine(tip, tip + QPointF(0, px * 0.26));

    return QIcon(pm);
}

// Circular refresh icon: ~280 deg arc with a filled arrowhead at the start.
inline QIcon reload(int px = 16, QColor color = QColor(0x9a, 0x9a, 0x9a))
{
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    detail::primeLine(p, px, color, 1.0 / 9.0);

    const qreal m = px * 0.20;
    const QRectF r(m, m, px - 2 * m, px - 2 * m);
    p.drawArc(r, 60 * 16, 280 * 16);

    // Arrowhead at the start of the arc (upper-right); the triangle splays
    // along the tangent at 60 deg, which points up-and-left for CCW.
    constexpr qreal kPi = 3.14159265358979323846;
    const qreal ang = 60.0 * kPi / 180.0;
    const qreal cx = r.center().x();
    const qreal cy = r.center().y();
    const qreal rr = r.width() / 2.0;
    const qreal x = cx + rr * std::cos(ang);
    const qreal y = cy - rr * std::sin(ang);

    const qreal s = px * 0.18;
    const qreal rx = std::cos(ang), ry = -std::sin(ang); // outward radial
    const qreal tx = std::sin(ang), ty = std::cos(ang);  // tangent (CCW)

    p.setPen(Qt::NoPen);
    p.setBrush(color);
    QPolygonF head;
    head << QPointF(x + rx * s, y + ry * s) << QPointF(x - rx * s, y - ry * s)
         << QPointF(x + tx * s * 1.5, y + ty * s * 1.5);
    p.drawPolygon(head);

    return QIcon(pm);
}

inline QIcon plus(int px = 16, QColor color = QColor(0x9a, 0x9a, 0x9a))
{
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    detail::primeLine(p, px, color, 1.0 / 8.0);

    const qreal m = px * 0.25;
    p.drawLine(QPointF(px / 2.0, m), QPointF(px / 2.0, px - m));
    p.drawLine(QPointF(m, px / 2.0), QPointF(px - m, px / 2.0));
    return QIcon(pm);
}

// Solid right-pointing triangle, nudged right of center so a square button
// reads as visually balanced.
inline QIcon play(int px = 16, QColor color = QColor(0x77, 0xaa, 0xdd))
{
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);

    const qreal m = px * 0.22;
    QPolygonF tri;
    tri << QPointF(m, m) << QPointF(px - m * 0.7, px / 2.0) << QPointF(m, px - m);
    p.drawPolygon(tri);
    return QIcon(pm);
}

// Solid rounded square for the interrupt button.
inline QIcon stopSquare(int px = 16, QColor color = QColor(0xcc, 0x44, 0x44))
{
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);

    const qreal m = px * 0.27;
    p.drawRoundedRect(QRectF(m, m, px - 2 * m, px - 2 * m), 1.5, 1.5);
    return QIcon(pm);
}

// ---- Line art

// Diagonal cross, for the remove/close/dismiss buttons.
inline QIcon close(int px = 16, QColor color = QColor(0x9a, 0x9a, 0x9a))
{
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    detail::primeLine(p, px, color, 1.0 / 8.0);

    const qreal m = px * 0.28;
    p.drawLine(QPointF(m, m), QPointF(px - m, px - m));
    p.drawLine(QPointF(px - m, m), QPointF(m, px - m));
    return QIcon(pm);
}

inline QIcon minus(int px = 16, QColor color = QColor(0x9a, 0x9a, 0x9a))
{
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    detail::primeLine(p, px, color, 1.0 / 8.0);

    const qreal m = px * 0.25;
    p.drawLine(QPointF(m, px / 2.0), QPointF(px - m, px / 2.0));
    return QIcon(pm);
}

namespace detail {

// Half-circle sweep with an arrowhead on the left tip. Redo mirrors it.
inline void curvedArrow(QPainter& p, int px)
{
    const qreal m = px * 0.16;
    const QRectF r(m, px * 0.30, px - 2 * m, (px - 2 * m) * 0.72);
    p.drawArc(r, 0, 180 * 16);

    const QPointF tip(r.left(), r.center().y() + px * 0.03);
    const qreal s = px * 0.19;
    p.drawLine(tip, tip + QPointF(-s * 0.85, -s));
    p.drawLine(tip, tip + QPointF(s * 0.85, -s));
}

} // namespace detail

inline QIcon undo(int px = 16, QColor color = QColor(0x9a, 0x9a, 0x9a))
{
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    detail::primeLine(p, px, color, 1.0 / 9.0);
    detail::curvedArrow(p, px);
    return QIcon(pm);
}

inline QIcon redo(int px = 16, QColor color = QColor(0x9a, 0x9a, 0x9a))
{
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    detail::primeLine(p, px, color, 1.0 / 9.0);
    p.translate(px, 0);
    p.scale(-1, 1);
    detail::curvedArrow(p, px);
    return QIcon(pm);
}

// Down arrow landing on a bar: the "pushed into the composer" affordance.
inline QIcon pushDown(int px = 16, QColor color = QColor(0x9a, 0x9a, 0x9a))
{
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    detail::primeLine(p, px, color, 1.0 / 9.0);

    const qreal cx = px / 2.0;
    const qreal tipY = px * 0.64;
    const qreal h = px * 0.19;

    p.drawLine(QPointF(cx, px * 0.16), QPointF(cx, tipY));
    p.drawLine(QPointF(cx, tipY), QPointF(cx - h, tipY - h));
    p.drawLine(QPointF(cx, tipY), QPointF(cx + h, tipY - h));
    p.drawLine(QPointF(cx - px * 0.26, px * 0.84), QPointF(cx + px * 0.26, px * 0.84));
    return QIcon(pm);
}

// Three stacked rules; the collapsed-list handle.
inline QIcon menuLines(int px = 16, QColor color = QColor(0x9a, 0x9a, 0x9a))
{
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    detail::primeLine(p, px, color, 1.0 / 10.0);

    const qreal m = px * 0.18;
    for (int i = 0; i < 3; ++i) {
        const qreal y = px * 0.28 + i * px * 0.22;
        p.drawLine(QPointF(m, y), QPointF(px - m, y));
    }
    return QIcon(pm);
}

// Pennant on a staff; marks a rule that forces its tag through.
inline QIcon flag(int px = 16, QColor color = QColor(0x9a, 0x9a, 0x9a))
{
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    detail::primeLine(p, px, color, 1.0 / 9.0);

    const qreal x = px * 0.28;
    p.drawLine(QPointF(x, px * 0.14), QPointF(x, px * 0.88));

    QPolygonF pennant;
    pennant << QPointF(x, px * 0.16) << QPointF(px * 0.82, px * 0.33)
            << QPointF(x, px * 0.50);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPolygon(pennant);
    return QIcon(pm);
}

// Opposed arrows; marks a rule that swaps its tag for another.
inline QIcon swap(int px = 16, QColor color = QColor(0x9a, 0x9a, 0x9a))
{
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    detail::primeLine(p, px, color, 1.0 / 10.0);

    const qreal m = px * 0.16;
    const qreal h = px * 0.15;
    const qreal top = px * 0.36;
    const qreal bot = px * 0.64;

    p.drawLine(QPointF(m, top), QPointF(px - m, top));
    p.drawLine(QPointF(px - m, top), QPointF(px - m - h, top - h));
    p.drawLine(QPointF(px - m, top), QPointF(px - m - h, top + h));

    p.drawLine(QPointF(m, bot), QPointF(px - m, bot));
    p.drawLine(QPointF(m, bot), QPointF(m + h, bot - h));
    p.drawLine(QPointF(m, bot), QPointF(m + h, bot + h));
    return QIcon(pm);
}

namespace detail {

inline void primeSolid(QPainter& p, QColor color)
{
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
}

// Right-pointing triangle; caretLeft mirrors it.
inline void caretPath(QPainter& p, int px)
{
    const qreal mx = px * 0.34;
    const qreal my = px * 0.24;
    QPolygonF tri;
    tri << QPointF(mx, my) << QPointF(px - mx, px / 2.0) << QPointF(mx, px - my);
    p.drawPolygon(tri);
}

// Left-pointing line arrow; right and up are the same path transformed.
inline void arrowPath(QPainter& p, int px)
{
    const qreal m = px * 0.22;
    const qreal cy = px / 2.0;
    const qreal h = px * 0.20;
    p.drawLine(QPointF(px - m, cy), QPointF(m, cy));
    p.drawLine(QPointF(m, cy), QPointF(m + h, cy - h));
    p.drawLine(QPointF(m, cy), QPointF(m + h, cy + h));
}

} // namespace detail

// ---- Solid carets, for the image pager

inline QIcon caretRight(int px = 16, QColor color = QColor(0x9a, 0x9a, 0x9a))
{
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    detail::primeSolid(p, color);
    detail::caretPath(p, px);
    return QIcon(pm);
}

inline QIcon caretLeft(int px = 16, QColor color = QColor(0x9a, 0x9a, 0x9a))
{
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    detail::primeSolid(p, color);
    p.translate(px, 0);
    p.scale(-1, 1);
    detail::caretPath(p, px);
    return QIcon(pm);
}

// ---- Line arrows, for history navigation and the injected-tag badge

inline QIcon arrowLeft(int px = 16, QColor color = QColor(0x9a, 0x9a, 0x9a))
{
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    detail::primeLine(p, px, color, 1.0 / 9.0);
    detail::arrowPath(p, px);
    return QIcon(pm);
}

inline QIcon arrowRight(int px = 16, QColor color = QColor(0x9a, 0x9a, 0x9a))
{
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    detail::primeLine(p, px, color, 1.0 / 9.0);
    p.translate(px, 0);
    p.scale(-1, 1);
    detail::arrowPath(p, px);
    return QIcon(pm);
}

inline QIcon arrowUp(int px = 16, QColor color = QColor(0x9a, 0x9a, 0x9a))
{
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    detail::primeLine(p, px, color, 1.0 / 9.0);
    p.translate(px / 2.0, px / 2.0);
    p.rotate(90);
    p.translate(-px / 2.0, -px / 2.0);
    detail::arrowPath(p, px);
    return QIcon(pm);
}

// ---- Button state plumbing
//
// QSS color rules reach glyph text but not a painted QIcon, so buttons that
// used to take their hover feedback from a color rule supply the renderings
// here instead. Disabled is folded into the QIcon and chosen automatically;
// QPushButton has no icon mode for hover, so that one needs an event filter.

namespace detail {

class HoverIcons : public QObject
{
public:
    HoverIcons(QAbstractButton* btn, QIcon rest, QIcon hover)
        : QObject(btn), m_btn(btn), m_rest(std::move(rest)), m_hover(std::move(hover))
    {
        m_btn->setIcon(m_rest);
        m_btn->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject* o, QEvent* e) override
    {
        if (o == m_btn) {
            if (e->type() == QEvent::Enter) m_btn->setIcon(m_hover);
            else if (e->type() == QEvent::Leave) m_btn->setIcon(m_rest);
        }
        return QObject::eventFilter(o, e);
    }

private:
    QAbstractButton* m_btn;
    QIcon m_rest;
    QIcon m_hover;
};

} // namespace detail

using Factory = QIcon (*)(int, QColor);

// Paints fn once per visual state and wires the set to btn. Leave disabled
// invalid for buttons that never go insensitive.
inline void applyStates(QAbstractButton* btn, Factory fn, int px, QColor rest,
                        QColor hover, QColor disabled = QColor())
{
    QIcon restIcon = fn(px, rest);
    QIcon hoverIcon = fn(px, hover);
    if (disabled.isValid()) {
        const QPixmap off = fn(px, disabled).pixmap(px, px);
        restIcon.addPixmap(off, QIcon::Disabled);
        hoverIcon.addPixmap(off, QIcon::Disabled);
    }
    btn->setIconSize(QSize(px, px));
    new detail::HoverIcons(btn, std::move(restIcon), std::move(hoverIcon));
}

} // namespace tc::icons
