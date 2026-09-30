#include <app/movable_preview_label.h>
#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QDrag>
#include <QFileInfo>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>
#include <QUrl>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

constexpr int kGripSide = 16;
constexpr int kGripLines = 3;
constexpr int kOutputButtonSide = 24;

// The newest image under `dir` by modification time. Recursive, because a
// save node's filename prefix puts files in a per-workflow subfolder
// (<output>/<date>/<workflow>/foo_00001.png), so a flat scan finds nothing.
QString newestImageIn(const QString& dir)
{
    if (dir.isEmpty() || !QDir(dir).exists()) return {};

    static const QStringList filters = {u"*.png"_s, u"*.jpg"_s, u"*.jpeg"_s, u"*.webp"_s};

    QString newestPath;
    QDateTime newestTime;

    QDirIterator it(dir, filters, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QDateTime modified = it.fileInfo().lastModified();
        if (!newestPath.isEmpty() && modified <= newestTime) continue;
        newestTime = modified;
        newestPath = it.fileInfo().absoluteFilePath();
    }
    return newestPath;
}

} // namespace

MovablePreviewLabel::MovablePreviewLabel(QWidget* parent) : QLabel(parent)
{
    setCursor(Qt::PointingHandCursor);
    setAlignment(Qt::AlignCenter);
    setAttribute(Qt::WA_StyledBackground);
    setMouseTracking(true);
    m_outputIcon = QPixmap(u":/icons/nav_output.png"_s);
}

void MovablePreviewLabel::setFilePath(const QString& path)
{
    m_path = path;
}

void MovablePreviewLabel::setSourcePixmap(const QPixmap& pixmap)
{
    const bool wasNull = m_source.isNull();
    const bool nowNull = pixmap.isNull();
    const bool aspectChanged = wasNull != nowNull
        || (!nowNull && !wasNull
            && qreal(pixmap.width()) * m_source.height()
                != qreal(pixmap.height()) * m_source.width());

    m_source = pixmap;
    updateScaled();
    if (!aspectChanged) return;

    if (!m_userPlaced) {
        autoFit();
        return;
    }
    if (nowNull || pixmap.width() <= 0 || pixmap.height() <= 0) return;

    // Re-derive the size the way a grip resize would: take the larger of the
    // two axis scales, lock to the source aspect, clamp to the minimum and to
    // the bounds. The bottom edge is the anchor, so it grows upward exactly
    // as a grip pull does.
    qreal scale = std::max(qreal(width()) / qreal(pixmap.width()),
                           qreal(height()) / qreal(pixmap.height()));
    scale = qMax(scale, qreal(m_minSide) / qreal(qMin(pixmap.width(), pixmap.height())));

    const QRect bounds = effectiveBounds();
    if (bounds.isValid()) {
        scale = qMin(scale, qreal(bounds.width()) / qreal(pixmap.width()));
        scale = qMin(scale, qreal(bounds.height()) / qreal(pixmap.height()));
    }

    const int newWidth = qRound(pixmap.width() * scale);
    const int newHeight = qRound(pixmap.height() * scale);

    int newX = x();
    int newY = y() + height() - newHeight;
    if (bounds.isValid()) {
        newX = qBound(bounds.left(), newX, bounds.right() - newWidth + 1);
        newY = qBound(bounds.top(), newY, bounds.bottom() - newHeight + 1);
    }

    move(newX, newY);
    resize(newWidth, newHeight);
}

void MovablePreviewLabel::setOutputFolder(const QString& path)
{
    m_outputFolder = path;
    update();
}

void MovablePreviewLabel::setTempFolder(const QString& path)
{
    m_tempFolder = path;
    update();
}

// A dated output pattern has no folder until the first save of the day, so
// the temp folder keeps the button useful until then.
void MovablePreviewLabel::openFolder() const
{
    QString target = m_outputFolder;
    if (target.isEmpty() || !QDir(target).exists()) target = m_tempFolder;
    if (target.isEmpty() || !QDir(target).exists()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(target));
}

// A click opens the saved output rather than the temp frame on screen: the
// temp folder holds in-progress decodes, so the file worth opening is the
// newest one the save node wrote.
void MovablePreviewLabel::openPreferredTarget() const
{
    QString target = newestImageIn(m_outputFolder);
    if (target.isEmpty()) target = m_path;
    if (target.isEmpty()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(target));
}

void MovablePreviewLabel::setMovableBounds(const QRect& bounds)
{
    m_movableBounds = bounds;
    if (m_userPlaced) clampToBounds();
}

bool MovablePreviewLabel::isUserPlaced() const
{
    return m_userPlaced;
}

void MovablePreviewLabel::clampToBounds()
{
    const QRect bounds = effectiveBounds();
    if (!bounds.isValid()) return;

    QSize wanted = size();
    wanted.setWidth(qBound(m_minSide, wanted.width(), bounds.width()));
    wanted.setHeight(qBound(m_minSide, wanted.height(), bounds.height()));

    QPoint where = pos();
    where.setX(qBound(bounds.left(), where.x(), bounds.right() - wanted.width() + 1));
    where.setY(qBound(bounds.top(), where.y(), bounds.bottom() - wanted.height() + 1));

    if (wanted != size()) resize(wanted);
    if (where != pos()) move(where);
}

QRect MovablePreviewLabel::effectiveBounds() const
{
    if (m_movableBounds.isValid()) return m_movableBounds;
    if (QWidget* parent = parentWidget()) return parent->rect();
    return {};
}

void MovablePreviewLabel::autoFit()
{
    if (m_userPlaced) return;

    const QRect bounds = effectiveBounds();
    if (!bounds.isValid()) return;

    QSize target;
    if (!m_source.isNull() && m_source.width() > 0 && m_source.height() > 0) {
        constexpr qreal kFillRatio = 0.5; // about half the available area
        const QSize cap(int(bounds.width() * kFillRatio), int(bounds.height() * kFillRatio));
        target = m_source.size().scaled(cap, Qt::KeepAspectRatio);
    } else {
        const int side = qBound(120, qMin(bounds.width(), bounds.height()) / 2, 600);
        target = QSize(side, side);
    }
    target.setWidth(qMax(m_minSide, target.width()));
    target.setHeight(qMax(m_minSide, target.height()));

    move(QPoint(bounds.left(), bounds.bottom() - target.height() + 1));
    resize(target);
}

QRect MovablePreviewLabel::gripRect() const
{
    return {width() - kGripSide, 0, kGripSide, kGripSide};
}

QRect MovablePreviewLabel::outputButtonRect() const
{
    if ((m_outputFolder.isEmpty() && m_tempFolder.isEmpty()) || m_outputIcon.isNull()) return {};
    return {0, 0, kOutputButtonSide, kOutputButtonSide};
}

void MovablePreviewLabel::updateHoverCursor(const QPoint& pos)
{
    setCursor(gripRect().contains(pos) ? Qt::SizeBDiagCursor : Qt::PointingHandCursor);
}

void MovablePreviewLabel::resizeEvent(QResizeEvent* event)
{
    QLabel::resizeEvent(event);
    updateScaled();
}

void MovablePreviewLabel::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton) {
        m_mode = Mode::Moving;
        m_dragStartGlobal = event->globalPosition().toPoint();
        m_dragStartTopLeft = pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && outputButtonRect().contains(event->pos())) {
        // Opened on release, so the release can tell this from a click on the
        // image and dragging off the button cancels it.
        m_pressOnFolderButton = true;
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && gripRect().contains(event->pos())) {
        m_mode = Mode::Resizing;
        m_dragStartGlobal = event->globalPosition().toPoint();
        m_dragStartTopLeft = pos();
        m_dragStartSize = size();
        setCursor(Qt::SizeBDiagCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        m_pressPos = event->pos();
        m_dragInFlight = false;
    }
    QLabel::mousePressEvent(event);
}

void MovablePreviewLabel::mouseMoveEvent(QMouseEvent* event)
{
    if (m_mode == Mode::Moving && (event->buttons() & Qt::RightButton)) {
        const QPoint delta = event->globalPosition().toPoint() - m_dragStartGlobal;
        QPoint where = m_dragStartTopLeft + delta;

        const QRect bounds = effectiveBounds();
        if (bounds.isValid()) {
            where.setX(qBound(bounds.left(), where.x(), bounds.right() - width() + 1));
            where.setY(qBound(bounds.top(), where.y(), bounds.bottom() - height() + 1));
        }
        move(where);
        event->accept();
        return;
    }

    if (m_mode == Mode::Resizing && (event->buttons() & Qt::LeftButton)) {
        // The grip is top-right: width grows with +dx and height with -dy.
        // The left edge stays put and the top follows, anchoring the bottom.
        const QPoint delta = event->globalPosition().toPoint() - m_dragStartGlobal;
        const int newX = m_dragStartTopLeft.x();
        int newWidth = m_dragStartSize.width() + delta.x();
        int newHeight = m_dragStartSize.height() - delta.y();
        int newY = m_dragStartTopLeft.y() + delta.y();

        if (!m_source.isNull() && m_source.width() > 0 && m_source.height() > 0) {
            // Locked to the source aspect: whichever axis was pushed further
            // proportionally drives the scale, and the other follows.
            const qreal scaleW = qreal(newWidth) / qreal(m_source.width());
            const qreal scaleH = qreal(newHeight) / qreal(m_source.height());
            const qreal minScale =
                qreal(m_minSide) / qreal(qMin(m_source.width(), m_source.height()));
            qreal scale = std::max({scaleW, scaleH, minScale});

            const QRect bounds = effectiveBounds();
            if (bounds.isValid()) {
                const int maxWidth = bounds.right() + 1 - newX;
                if (maxWidth > 0) scale = qMin(scale, qreal(maxWidth) / qreal(m_source.width()));

                const int bottom = m_dragStartTopLeft.y() + m_dragStartSize.height();
                const int maxHeight = bottom - bounds.top();
                if (maxHeight > 0)
                    scale = qMin(scale, qreal(maxHeight) / qreal(m_source.height()));
            }

            newWidth = qRound(m_source.width() * scale);
            newHeight = qRound(m_source.height() * scale);
            newY = m_dragStartTopLeft.y() + m_dragStartSize.height() - newHeight;
        } else {
            // Nothing to lock to, so resize freely.
            if (newWidth < m_minSide) newWidth = m_minSide;
            if (newHeight < m_minSide) {
                newY = m_dragStartTopLeft.y() + m_dragStartSize.height() - m_minSide;
                newHeight = m_minSide;
            }

            const QRect bounds = effectiveBounds();
            if (bounds.isValid()) {
                if (newY < bounds.top()) {
                    newHeight -= bounds.top() - newY;
                    newY = bounds.top();
                }
                if (newX + newWidth > bounds.right() + 1)
                    newWidth = bounds.right() + 1 - newX;
            }
        }

        move(newX, newY);
        resize(newWidth, newHeight);
        event->accept();
        return;
    }

    if (!(event->buttons() & (Qt::LeftButton | Qt::RightButton)))
        updateHoverCursor(event->pos());

    if (m_dragInFlight || m_path.isEmpty() || !(event->buttons() & Qt::LeftButton)) {
        QLabel::mouseMoveEvent(event);
        return;
    }
    if ((event->pos() - m_pressPos).manhattanLength() < QApplication::startDragDistance()) {
        QLabel::mouseMoveEvent(event);
        return;
    }

    m_dragInFlight = true;

    auto* mime = new QMimeData;
    mime->setUrls({QUrl::fromLocalFile(m_path)});

    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    if (!m_source.isNull()) {
        const QPixmap thumb =
            m_source.scaled(160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        drag->setPixmap(thumb);
        drag->setHotSpot(QPoint(thumb.width() / 2, thumb.height() / 2));
    }
    drag->exec(Qt::CopyAction);
}

void MovablePreviewLabel::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_pressOnFolderButton) {
        m_pressOnFolderButton = false;
        if (outputButtonRect().contains(event->pos())) openFolder();
        event->accept();
        return;
    }

    if (m_mode != Mode::Idle) {
        // Only a gesture that actually changed the geometry counts as
        // placing it, so a stray right click does not lock the auto layout.
        const bool moved = pos() != m_dragStartTopLeft || size() != m_dragStartSize;
        if (moved) m_userPlaced = true;

        const bool wasGrip = m_mode == Mode::Resizing;
        m_mode = Mode::Idle;
        updateHoverCursor(event->pos());

        // Press and release on the grip with no drag is a click.
        if (wasGrip && !moved) openPreferredTarget();
        event->accept();
        return;
    }

    // Suppressed after a drag, or releasing inside the label right after a
    // quick drag would also open the viewer.
    if (event->button() == Qt::LeftButton && !m_dragInFlight && rect().contains(event->pos()))
        openPreferredTarget();

    m_dragInFlight = false;
    QLabel::mouseReleaseEvent(event);
}

void MovablePreviewLabel::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    update();
    QLabel::enterEvent(event);
}

void MovablePreviewLabel::leaveEvent(QEvent* event)
{
    m_hovered = false;
    update();
    if (m_mode == Mode::Idle) setCursor(Qt::PointingHandCursor);
    QLabel::leaveEvent(event);
}

void MovablePreviewLabel::paintEvent(QPaintEvent* event)
{
    QLabel::paintEvent(event);
    if (!m_hovered && m_mode == Mode::Idle) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPen pen(QColor(255, 255, 255, 150));
    pen.setWidth(2);
    painter.setPen(pen);

    constexpr int pad = 3;
    constexpr int step = 4;
    constexpr int length = 8;

    // The grip, top right, its diagonals matching the resize cursor.
    for (int i = 0; i < kGripLines; ++i) {
        const int offset = pad + i * step;
        painter.drawLine(width() - offset - length, offset, width() - offset, offset + length);
    }

    const QRect button = outputButtonRect();
    if (button.isEmpty()) return;

    constexpr int iconPad = 4;
    const QRect iconRect = button.adjusted(iconPad, iconPad, -iconPad, -iconPad);
    painter.drawPixmap(iconRect, m_outputIcon.scaled(iconRect.size(), Qt::KeepAspectRatio,
                                                     Qt::SmoothTransformation));
}

void MovablePreviewLabel::updateScaled()
{
    if (m_source.isNull() || width() <= 0 || height() <= 0) return;
    setPixmap(m_source.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

} // namespace tc
