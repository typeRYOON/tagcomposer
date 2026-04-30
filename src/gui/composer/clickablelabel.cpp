#include <gui/composer/clickablelabel.h>
#include <QDesktopServices>
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
    if (e->button() == Qt::LeftButton && !m_path.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_path));
    QLabel::mousePressEvent(e);
}

void ClickableLabel::updateScaled()
{
    if (!m_src.isNull() && width() > 0 && height() > 0)
        setPixmap(m_src.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

} // namespace gui
