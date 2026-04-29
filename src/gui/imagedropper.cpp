#include <gui/imagedropper.h>
#include <QPainter>
#include <QPainterPath>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMouseEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QDesktopServices>

namespace gui {

static const QStringList IMAGE_EXTS = { "jpg", "jpeg", "png", "webp", "bmp", "gif" };

ImageDropper::ImageDropper(QWidget* parent)
    : QLabel(parent)
{
    setObjectName("ImageDropper");
    setAcceptDrops(true);
    setFixedSize(200, 257);
}

void ImageDropper::setImage(const QString& path)
{
    m_path   = path;
    m_pixmap = QPixmap(path);
    update();
}

void ImageDropper::clearImage()
{
    m_path   = {};
    m_pixmap = QPixmap();
    update();
}

void ImageDropper::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && !m_path.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_path));
    QLabel::mousePressEvent(e);
}

void ImageDropper::dragEnterEvent(QDragEnterEvent* e)
{
    if (!e->mimeData()->hasUrls()) return;
    for (const QUrl& url : e->mimeData()->urls()) {
        if (IMAGE_EXTS.contains(QFileInfo(url.toLocalFile()).suffix().toLower())) {
            m_dragOver = true;
            update();
            e->acceptProposedAction();
            return;
        }
    }
}

void ImageDropper::dragLeaveEvent(QDragLeaveEvent*)
{
    m_dragOver = false;
    update();
}

void ImageDropper::dropEvent(QDropEvent* e)
{
    m_dragOver = false;
    for (const QUrl& url : e->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (IMAGE_EXTS.contains(QFileInfo(path).suffix().toLower())) {
            setImage(path);
            emit imageDropped(path);
            break;
        }
    }
    e->acceptProposedAction();
}

void ImageDropper::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    const QRectF r      = QRectF(rect());
    const qreal  radius = 8.0;

    QPainterPath clip;
    clip.addRoundedRect(r, radius, radius);
    p.setClipPath(clip);

    if (!m_pixmap.isNull()) {
        const QSizeF scaled = m_pixmap.size().scaled(size(), Qt::KeepAspectRatioByExpanding);
        const QRectF dst(
            (r.width()  - scaled.width())  / 2.0,
            (r.height() - scaled.height()) / 2.0,
            scaled.width(), scaled.height()
        );
        p.drawPixmap(dst.toRect(), m_pixmap);
    } else {
        p.fillPath(clip, QColor(0x18, 0x18, 0x18));
        p.setClipping(false);
        QPen dashedPen(QColor(0x38, 0x38, 0x38), 1.5, Qt::DashLine);
        p.setPen(dashedPen);
        p.drawRoundedRect(r.adjusted(1, 1, -1, -1), radius, radius);

        p.setPen(QColor(0x40, 0x40, 0x40));
        QFont f = font(); f.setPointSize(9);
        p.setFont(f);
        p.drawText(r, Qt::AlignCenter, "Drop image");
    }

    if (m_dragOver) {
        p.setClipping(false);
        p.setPen(QPen(QColor(0x40, 0x80, 0xff), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(1, 1, -1, -1), radius, radius);
    }
}

} // namespace gui
