#include <gui/composer/clickablelabel.h>
#include <QApplication>
#include <QDesktopServices>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QUrl>

namespace gui {

ClickableLabel::ClickableLabel(QWidget* parent) : QLabel(parent)
{
    setCursor(Qt::PointingHandCursor);
    setAlignment(Qt::AlignCenter);
    setAttribute(Qt::WA_StyledBackground);
}

void ClickableLabel::setFilePath(const QString& path)
{
    m_path = path;
}

void ClickableLabel::setSourcePixmap(const QPixmap& pix)
{
    m_src = pix;
    updateScaled();
}

void ClickableLabel::resizeEvent(QResizeEvent* e)
{
    QLabel::resizeEvent(e);
    updateScaled();
}

void ClickableLabel::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_pressPos     = e->pos();
        m_dragInFlight = false;
    }
    QLabel::mousePressEvent(e);
}

void ClickableLabel::mouseMoveEvent(QMouseEvent* e)
{
    if (m_dragInFlight || m_path.isEmpty()
        || !(e->buttons() & Qt::LeftButton)) {
        QLabel::mouseMoveEvent(e);
        return;
    }
    if ((e->pos() - m_pressPos).manhattanLength()
        < QApplication::startDragDistance()) {
        QLabel::mouseMoveEvent(e);
        return;
    }

    m_dragInFlight = true;
    auto* mime = new QMimeData;
    mime->setUrls({ QUrl::fromLocalFile(m_path) });

    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    if (!m_src.isNull()) {
        const QPixmap thumb = m_src.scaled(
            160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        drag->setPixmap(thumb);
        drag->setHotSpot(QPoint(thumb.width() / 2, thumb.height() / 2));
    }
    drag->exec(Qt::CopyAction);
}

void ClickableLabel::mouseReleaseEvent(QMouseEvent* e)
{
    // Suppress the click-open when a drag was just started; otherwise
    // releasing inside the label after a quick click opens the OS viewer.
    if (e->button() == Qt::LeftButton
        && !m_dragInFlight
        && !m_path.isEmpty()
        && rect().contains(e->pos())) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_path));
    }
    m_dragInFlight = false;
    QLabel::mouseReleaseEvent(e);
}

void ClickableLabel::updateScaled()
{
    if (!m_src.isNull() && width() > 0 && height() > 0)
        setPixmap(m_src.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

} // namespace gui
