#include <gui/composer/clickablelabel.h>
#include <QApplication>
#include <QColor>
#include <QDesktopServices>
#include <QDir>
#include <QDrag>
#include <QFileInfo>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QResizeEvent>
#include <QUrl>
#include <algorithm>

namespace gui {

namespace {
constexpr int kGripSide = 16;
constexpr int kGripLines = 3;
constexpr int kOutputBtnSide = 24;
} // namespace

ClickableLabel::ClickableLabel(QWidget* parent) : QLabel(parent)
{
    setCursor(Qt::PointingHandCursor);
    setAlignment(Qt::AlignCenter);
    setAttribute(Qt::WA_StyledBackground);
    setMouseTracking(true);
    m_outputIcon = QPixmap(":/icons/nav_output.png");
}

void ClickableLabel::setFilePath(const QString& path)
{
    m_path = path;
}

void ClickableLabel::setSourcePixmap(const QPixmap& pix)
{
    const bool aspectChanged =
        m_src.isNull() != pix.isNull() ||
        (!pix.isNull() && !m_src.isNull() &&
         qreal(pix.width()) * m_src.height() != qreal(pix.height()) * m_src.width());
    m_src = pix;
    updateScaled();
    if (!m_userPlaced && aspectChanged) autoFit();
}

void ClickableLabel::setOutputFolder(const QString& path)
{
    m_outputFolder = path;
    update();
}

void ClickableLabel::setMovableBounds(const QRect& r)
{
    m_movableBounds = r;
    if (m_userPlaced) clampToBounds();
}

void ClickableLabel::clampToBounds()
{
    const QRect b = effectiveBounds();
    if (!b.isValid()) return;

    QSize s = size();
    s.setWidth(qBound(m_minSide, s.width(), b.width()));
    s.setHeight(qBound(m_minSide, s.height(), b.height()));

    QPoint p = pos();
    p.setX(qBound(b.left(), p.x(), b.right() - s.width() + 1));
    p.setY(qBound(b.top(), p.y(), b.bottom() - s.height() + 1));

    if (s != size()) resize(s);
    if (p != pos()) move(p);
}

QRect ClickableLabel::effectiveBounds() const
{
    if (m_movableBounds.isValid()) return m_movableBounds;
    if (auto* p = parentWidget()) return p->rect();
    return QRect();
}

void ClickableLabel::autoFit()
{
    if (m_userPlaced) return;
    const QRect b = effectiveBounds();
    if (!b.isValid()) return;

    QSize target;
    if (!m_src.isNull() && m_src.width() > 0 && m_src.height() > 0) {
        // Cap to ~half the available area, pixmap-aspect-locked.
        constexpr qreal kFillRatio = 0.5;
        const QSize cap(int(b.width() * kFillRatio), int(b.height() * kFillRatio));
        target = m_src.size().scaled(cap, Qt::KeepAspectRatio);
    }
    else {
        const int side = qBound(120, qMin(b.width(), b.height()) / 2, 600);
        target = QSize(side, side);
    }
    target.setWidth(qMax(m_minSide, target.width()));
    target.setHeight(qMax(m_minSide, target.height()));

    // Bottom-left anchor inside bounds.
    const QPoint topLeft(b.left(), b.bottom() - target.height() + 1);
    move(topLeft);
    resize(target);
}

QRect ClickableLabel::gripRect() const
{
    return QRect(width() - kGripSide, 0, kGripSide, kGripSide);
}

QRect ClickableLabel::outputBtnRect() const
{
    if (m_outputFolder.isEmpty() || m_outputIcon.isNull()) return {};
    return QRect(0, 0, kOutputBtnSide, kOutputBtnSide);
}

void ClickableLabel::updateHoverCursor(const QPoint& pos)
{
    if (gripRect().contains(pos))
        setCursor(Qt::SizeBDiagCursor);
    else if (outputBtnRect().contains(pos))
        setCursor(Qt::PointingHandCursor);
    else
        setCursor(Qt::PointingHandCursor);
}


void ClickableLabel::resizeEvent(QResizeEvent* e)
{
    QLabel::resizeEvent(e);
    updateScaled();
}

void ClickableLabel::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::RightButton) {
        m_mode = Mode::Moving;
        m_dragStartGlobal = e->globalPosition().toPoint();
        m_dragStartTopLeft = pos();
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    if (e->button() == Qt::LeftButton && outputBtnRect().contains(e->pos())) {
        if (!m_outputFolder.isEmpty()) {
            // Open the most recent image in the folder; fall back to the
            // folder itself when empty/missing so the click is never a no-op.
            static const QStringList filters = {"*.png", "*.jpg", "*.jpeg", "*.webp"};
            const QFileInfoList files =
                QDir(m_outputFolder).entryInfoList(filters, QDir::Files);
            QString target = m_outputFolder;
            if (!files.isEmpty()) {
                const QFileInfo* newest = &files[0];
                for (const QFileInfo& fi : files)
                    if (fi.lastModified() > newest->lastModified()) newest = &fi;
                target = newest->absoluteFilePath();
            }
            QDesktopServices::openUrl(QUrl::fromLocalFile(target));
        }
        e->accept();
        return;
    }
    if (e->button() == Qt::LeftButton && gripRect().contains(e->pos())) {
        m_mode = Mode::Resizing;
        m_dragStartGlobal = e->globalPosition().toPoint();
        m_dragStartTopLeft = pos();
        m_dragStartSize = size();
        setCursor(Qt::SizeBDiagCursor);
        e->accept();
        return;
    }
    if (e->button() == Qt::LeftButton) {
        m_pressPos = e->pos();
        m_dragInFlight = false;
    }
    QLabel::mousePressEvent(e);
}

void ClickableLabel::mouseMoveEvent(QMouseEvent* e)
{
    if (m_mode == Mode::Moving && (e->buttons() & Qt::RightButton)) {
        const QPoint delta = e->globalPosition().toPoint() - m_dragStartGlobal;
        QPoint np = m_dragStartTopLeft + delta;
        const QRect b = effectiveBounds();
        if (b.isValid()) {
            np.setX(qBound(b.left(), np.x(), b.right() - width() + 1));
            np.setY(qBound(b.top(), np.y(), b.bottom() - height() + 1));
        }
        move(np);
        e->accept();
        return;
    }
    if (m_mode == Mode::Resizing && (e->buttons() & Qt::LeftButton)) {
        // Top-right grip: width grows with +dx, height grows with -dy.
        // Left edge stays put; top edge follows so the bottom is anchored.
        const QPoint delta = e->globalPosition().toPoint() - m_dragStartGlobal;
        const int newX = m_dragStartTopLeft.x();
        int newW = m_dragStartSize.width() + delta.x();
        int newH = m_dragStartSize.height() - delta.y();
        int newY = m_dragStartTopLeft.y() + delta.y();

        // Aspect-lock to the source pixmap. Whichever axis the user pushed
        // more (proportionally) drives the scale; the other follows.
        if (!m_src.isNull() && m_src.width() > 0 && m_src.height() > 0) {
            const qreal sw = qreal(newW) / qreal(m_src.width());
            const qreal sh = qreal(newH) / qreal(m_src.height());
            const qreal minS =
                qreal(m_minSide) / qreal(qMin(m_src.width(), m_src.height()));
            qreal s = std::max({sw, sh, minS});

            const QRect b = effectiveBounds();
            if (b.isValid()) {
                const int maxW = b.right() + 1 - newX;
                if (maxW > 0) s = qMin(s, qreal(maxW) / qreal(m_src.width()));
                const int bottom = m_dragStartTopLeft.y() + m_dragStartSize.height();
                const int maxH = bottom - b.top();
                if (maxH > 0) s = qMin(s, qreal(maxH) / qreal(m_src.height()));
            }

            newW = qRound(m_src.width() * s);
            newH = qRound(m_src.height() * s);
            newY = m_dragStartTopLeft.y() + m_dragStartSize.height() - newH;
        }
        else {
            // No pixmap to lock to - fall back to free resize.
            if (newW < m_minSide) newW = m_minSide;
            if (newH < m_minSide) {
                newY = m_dragStartTopLeft.y() + m_dragStartSize.height() - m_minSide;
                newH = m_minSide;
            }
            const QRect b = effectiveBounds();
            if (b.isValid()) {
                if (newY < b.top()) {
                    newH -= (b.top() - newY);
                    newY = b.top();
                }
                if (newX + newW > b.right() + 1) newW = b.right() + 1 - newX;
            }
        }

        move(newX, newY);
        resize(newW, newH);
        e->accept();
        return;
    }

    if (!(e->buttons() & (Qt::LeftButton | Qt::RightButton))) updateHoverCursor(e->pos());

    if (m_dragInFlight || m_path.isEmpty() || !(e->buttons() & Qt::LeftButton)) {
        QLabel::mouseMoveEvent(e);
        return;
    }
    if ((e->pos() - m_pressPos).manhattanLength() < QApplication::startDragDistance()) {
        QLabel::mouseMoveEvent(e);
        return;
    }

    m_dragInFlight = true;
    auto* mime = new QMimeData;
    mime->setUrls({QUrl::fromLocalFile(m_path)});

    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    if (!m_src.isNull()) {
        const QPixmap thumb = m_src.scaled(160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        drag->setPixmap(thumb);
        drag->setHotSpot(QPoint(thumb.width() / 2, thumb.height() / 2));
    }
    drag->exec(Qt::CopyAction);
}

void ClickableLabel::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_mode != Mode::Idle) {
        // Only mark user-placed if the gesture actually changed geometry,
        // so a stray right-click doesn't lock the auto-layout.
        if (pos() != m_dragStartTopLeft || size() != m_dragStartSize)
            m_userPlaced = true;
        m_mode = Mode::Idle;
        updateHoverCursor(e->pos());
        e->accept();
        return;
    }

    // Suppress the click-open when a drag was just started; otherwise
    // releasing inside the label after a quick click opens the OS viewer.
    if (e->button() == Qt::LeftButton && !m_dragInFlight && !m_path.isEmpty() &&
        rect().contains(e->pos())) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_path));
    }
    m_dragInFlight = false;
    QLabel::mouseReleaseEvent(e);
}

void ClickableLabel::enterEvent(QEnterEvent* e)
{
    m_hovered = true;
    update();
    QLabel::enterEvent(e);
}

void ClickableLabel::leaveEvent(QEvent* e)
{
    m_hovered = false;
    update();
    if (m_mode == Mode::Idle) setCursor(Qt::PointingHandCursor);
    QLabel::leaveEvent(e);
}

void ClickableLabel::paintEvent(QPaintEvent* e)
{
    QLabel::paintEvent(e);
    if (!m_hovered && m_mode == Mode::Idle) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(255, 255, 255, 150));
    pen.setWidth(2);
    p.setPen(pen);
    constexpr int pad = 3;
    constexpr int step = 4;
    constexpr int len = 8;
    const int w = width();
    // Resize grip - top-right corner, diagonals match the BDiag cursor.
    for (int i = 0; i < kGripLines; ++i) {
        const int off = pad + i * step;
        p.drawLine(w - off - len, off, w - off, off + len);
    }

    // Output-folder button - top-left, icon scaled to fit.
    const QRect btn = outputBtnRect();
    if (!btn.isEmpty()) {
        constexpr int iconPad = 4;
        const QRect iconRect = btn.adjusted(iconPad, iconPad, -iconPad, -iconPad);
        p.drawPixmap(iconRect,
                     m_outputIcon.scaled(iconRect.size(), Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation));
    }
}

void ClickableLabel::updateScaled()
{
    if (!m_src.isNull() && width() > 0 && height() > 0)
        setPixmap(m_src.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

} // namespace gui
