#include <app/image_dropper.h>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

const QStringList kImageSuffixes = {u"jpg"_s,  u"jpeg"_s, u"png"_s,
                                    u"webp"_s, u"bmp"_s,  u"gif"_s};

constexpr qreal kRadius = 8.0;

bool isImage(const QString& path)
{
    return kImageSuffixes.contains(QFileInfo(path).suffix().toLower());
}

} // namespace

ImageDropper::ImageDropper(QWidget* parent) : QLabel(parent)
{
    setObjectName(u"ImageDropper"_s);
    setAcceptDrops(true);
    setFixedSize(200, 257);
}

void ImageDropper::setImage(const QString& path)
{
    m_path = path;
    m_pixmap = QPixmap(path);
    update();
}

void ImageDropper::clearImage()
{
    m_path.clear();
    m_pixmap = QPixmap();
    update();
}

void ImageDropper::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !m_path.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_path));
    QLabel::mousePressEvent(event);
}

void ImageDropper::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls()) return;

    for (const QUrl& url : event->mimeData()->urls()) {
        if (!isImage(url.toLocalFile())) continue;
        m_dragOver = true;
        update();
        event->acceptProposedAction();
        return;
    }
}

void ImageDropper::dragLeaveEvent(QDragLeaveEvent*)
{
    m_dragOver = false;
    update();
}

void ImageDropper::dropEvent(QDropEvent* event)
{
    m_dragOver = false;

    for (const QUrl& url : event->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (!isImage(path)) continue;
        setImage(path);
        emit imageDropped(path);
        break;
    }
    event->acceptProposedAction();
}

void ImageDropper::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    const QRectF bounds(rect());
    QPainterPath clip;
    clip.addRoundedRect(bounds, kRadius, kRadius);
    painter.setClipPath(clip);

    if (!m_pixmap.isNull()) {
        const QSizeF scaled = m_pixmap.size().scaled(size(), Qt::KeepAspectRatioByExpanding);
        painter.drawPixmap(QRectF((bounds.width() - scaled.width()) / 2.0,
                                  (bounds.height() - scaled.height()) / 2.0, scaled.width(),
                                  scaled.height())
                               .toRect(),
                           m_pixmap);
    }
    else {
        painter.fillPath(clip, QColor(0x18, 0x18, 0x18));
        painter.setClipping(false);
        painter.setPen(QPen(QColor(0x38, 0x38, 0x38), 1.5, Qt::DashLine));
        painter.drawRoundedRect(bounds.adjusted(1, 1, -1, -1), kRadius, kRadius);

        QFont hint = font();
        hint.setPointSize(9);
        painter.setFont(hint);
        painter.setPen(QColor(0x40, 0x40, 0x40));
        painter.drawText(bounds, Qt::AlignCenter, u"Drop image"_s);
    }

    if (m_dragOver) {
        painter.setClipping(false);
        // Same green as the tile view's active marker, so every drop and
        // active affordance shares one accent.
        painter.setPen(QPen(QColor(0x4a, 0xa0, 0x4a), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(bounds.adjusted(1, 1, -1, -1), kRadius, kRadius);
    }
}

} // namespace tc
