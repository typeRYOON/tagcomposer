#include <gui/widgets/flowlayout.h>
#include <QWidget>

namespace gui {

FlowLayout::FlowLayout(QWidget* parent, int margin, int hSpacing, int vSpacing)
    : QLayout(parent), m_hSpace(hSpacing), m_vSpace(vSpacing)
{
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::FlowLayout(int margin, int hSpacing, int vSpacing)
    : m_hSpace(hSpacing), m_vSpace(vSpacing)
{
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout()
{
    QLayoutItem* item;
    while ((item = takeAt(0))) delete item;
}

void FlowLayout::addItem(QLayoutItem* item) { m_items.append(item); }

int FlowLayout::horizontalSpacing() const
{
    return m_hSpace >= 0 ? m_hSpace : smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
}

int FlowLayout::verticalSpacing() const
{
    return m_vSpace >= 0 ? m_vSpace : smartSpacing(QStyle::PM_LayoutVerticalSpacing);
}

int FlowLayout::count() const { return m_items.size(); }

QLayoutItem* FlowLayout::itemAt(int index) const
{
    return m_items.value(index);
}

QLayoutItem* FlowLayout::takeAt(int index)
{
    return (index >= 0 && index < m_items.size()) ? m_items.takeAt(index) : nullptr;
}

Qt::Orientations FlowLayout::expandingDirections() const { return {}; }

bool FlowLayout::hasHeightForWidth() const { return true; }

int FlowLayout::heightForWidth(int width) const
{
    return doLayout(QRect(0, 0, width, 0), true);
}

void FlowLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

QSize FlowLayout::sizeHint() const { return minimumSize(); }

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (const QLayoutItem* item : m_items) {
        if (item->isEmpty()) continue;  // skip hidden widgets
        size = size.expandedTo(item->minimumSize());
    }
    const QMargins m = contentsMargins();
    size += QSize(m.left() + m.right(), m.top() + m.bottom());
    return size;
}

int FlowLayout::doLayout(const QRect& rect, bool testOnly) const
{
    int left, top, right, bottom;
    getContentsMargins(&left, &top, &right, &bottom);
    const QRect effective = rect.adjusted(+left, +top, -right, -bottom);

    int x = effective.x();
    int y = effective.y();
    int lineHeight = 0;

    for (QLayoutItem* item : m_items) {
        // Skip hidden widgets so they don't reserve a slot in the flow
        // (Qt's stock FlowLayout example has this same bug).
        if (item->isEmpty()) continue;

        const QWidget* w = item->widget();
        int spaceX = horizontalSpacing();
        if (spaceX == -1)
            spaceX = w ? w->style()->layoutSpacing(
                        QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Horizontal)
                       : 0;
        int spaceY = verticalSpacing();
        if (spaceY == -1)
            spaceY = w ? w->style()->layoutSpacing(
                        QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Vertical)
                       : 0;

        int nextX = x + item->sizeHint().width() + spaceX;
        if (nextX - spaceX > effective.right() && lineHeight > 0) {
            x = effective.x();
            y += lineHeight + spaceY;
            nextX = x + item->sizeHint().width() + spaceX;
            lineHeight = 0;
        }

        if (!testOnly)
            item->setGeometry(QRect(QPoint(x, y), item->sizeHint()));

        x = nextX;
        lineHeight = qMax(lineHeight, item->sizeHint().height());
    }
    return y + lineHeight - rect.y() + bottom;
}

int FlowLayout::smartSpacing(QStyle::PixelMetric pm) const
{
    QObject* p = parent();
    if (!p) return -1;
    if (p->isWidgetType()) {
        QWidget* pw = static_cast<QWidget*>(p);
        return pw->style()->pixelMetric(pm, nullptr, pw);
    }
    return static_cast<QLayout*>(p)->spacing();
}

} // namespace gui
