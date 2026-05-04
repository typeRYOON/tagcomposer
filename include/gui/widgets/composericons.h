#pragma once
#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QRectF>
#include <cmath>

// Header-only icon factory. Paints small monochrome icons into QPixmaps and
// returns QIcons - keeps the in-app icons in sync with the rest of the dark
// theme without needing to ship a new PNG resource for every glyph. Used by
// header chips on the composer page (open / reload / save) and the run /
// interrupt buttons in the run bar.
namespace gui::icons {

namespace detail {

// Common painter setup for line-art icons. Returns a painter ready to draw
// strokes; brushes are no-fill, anti-aliasing is on, line caps/joins rounded.
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

// "Open in editor" - a small NE arrow leaving a half-frame. Conveys "leave
// this app and open elsewhere" the same way most browsers' external-link
// glyphs do.
inline QIcon openExternal(int px = 16, QColor color = QColor(0x9a, 0x9a, 0x9a))
{
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    detail::primeLine(p, px, color, 1.0 / 9.0);

    const qreal m = px * 0.20;
    const qreal cut = px * 0.45;

    // Bracket: opens at the upper-right corner.
    QPainterPath frame;
    frame.moveTo(px - m, px - cut);
    frame.lineTo(px - m, px - m);
    frame.lineTo(m, px - m);
    frame.lineTo(m, m);
    frame.lineTo(px - cut, m);
    p.drawPath(frame);

    // Diagonal shaft.
    const QPointF tail(px * 0.42, px * 0.58);
    const QPointF tip(px * 0.86, px * 0.14);
    p.drawLine(tail, tip);
    // Arrowhead - two short legs back from the tip.
    p.drawLine(tip, tip + QPointF(-px * 0.26, 0));
    p.drawLine(tip, tip + QPointF(0, px * 0.26));

    return QIcon(pm);
}

// Circular refresh / reload icon. ~280° arc with a filled triangular
// arrowhead at the start, evoking "rerun this".
inline QIcon reload(int px = 16, QColor color = QColor(0x9a, 0x9a, 0x9a))
{
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    detail::primeLine(p, px, color, 1.0 / 9.0);

    const qreal m = px * 0.20;
    const QRectF r(m, m, px - 2 * m, px - 2 * m);
    p.drawArc(r, 60 * 16, 280 * 16);

    // Arrowhead at the start of the arc (upper-right). Tangent direction at
    // 60° points up-and-left for CCW continuation; we splay a small triangle
    // along that tangent.
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

// Plus / "add new" - two crossed strokes.
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

// "Play" - solid right-pointing triangle. Centered slightly right of center
// so the visual balance feels right inside a square button.
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

// "Stop" - solid rounded square. Used for the interrupt button.
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

} // namespace gui::icons
