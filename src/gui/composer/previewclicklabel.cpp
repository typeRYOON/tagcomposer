#include <gui/composer/previewclicklabel.h>
#include <QMouseEvent>

namespace gui {

PreviewClickLabel::PreviewClickLabel(QWidget* parent) : QLabel(parent)
{
    setAttribute(Qt::WA_Hover);
    setAttribute(Qt::WA_StyledBackground);
    setCursor(Qt::PointingHandCursor);
}

void PreviewClickLabel::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) emit clicked();
    QLabel::mousePressEvent(e);
}

} // namespace gui
