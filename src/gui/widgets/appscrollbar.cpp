#include <gui/widgets/appscrollbar.h>
#include <QPainter>
#include <QStyleOptionSlider>

namespace gui {

static constexpr int kThickness = 6;

AppScrollBar::AppScrollBar(Qt::Orientation orientation, QWidget* parent)
    : QScrollBar(orientation, parent)
{
    setAttribute(Qt::WA_Hover);
    if (orientation == Qt::Vertical)
        setFixedWidth(kThickness);
    else
        setFixedHeight(kThickness);
}

AppScrollBar::AppScrollBar(QWidget* parent) : QScrollBar(parent)
{
    setAttribute(Qt::WA_Hover);
    setFixedWidth(kThickness);
}

void AppScrollBar::paintEvent(QPaintEvent*)
{
    QStyleOptionSlider opt;
    initStyleOption(&opt);

    const QRect handle =
        style()->subControlRect(QStyle::CC_ScrollBar, &opt, QStyle::SC_ScrollBarSlider, this);

    const bool pressed =
        (opt.activeSubControls & QStyle::SC_ScrollBarSlider) && (opt.state & QStyle::State_Sunken);
    const bool hovered = opt.state & QStyle::State_MouseOver;

    QColor handleColor;
    if (pressed)
        handleColor = {0x55, 0x55, 0x55};
    else if (hovered)
        handleColor = {0x3d, 0x3d, 0x3d};
    else
        handleColor = {0x2a, 0x2a, 0x2a};

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(handleColor);
    p.drawRoundedRect(handle, kThickness / 2, kThickness / 2);
}

} // namespace gui
