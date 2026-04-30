#include <gui/composer/scaledimagelabel.h>
#include <QResizeEvent>

namespace gui {

ScaledImageLabel::ScaledImageLabel(QWidget* parent) : QLabel(parent)
{
    setObjectName("PopoutImageLabel");
    setAlignment(Qt::AlignCenter);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(50, 50);
    setAttribute(Qt::WA_StyledBackground);
}

void ScaledImageLabel::setSourcePixmap(const QPixmap& pix)
{
    m_src = pix;
    updateScaled();
}

void ScaledImageLabel::resizeEvent(QResizeEvent* e)
{
    QLabel::resizeEvent(e);
    updateScaled();
}

void ScaledImageLabel::updateScaled()
{
    if (!m_src.isNull() && width() > 0 && height() > 0)
        setPixmap(m_src.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

} // namespace gui
