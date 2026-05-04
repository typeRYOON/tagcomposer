#pragma once
#include <QLayout>
#include <QRect>
#include <QStyle>

namespace gui {

// Wrapping horizontal layout - items flow left-to-right and wrap to the next
// row when they don't fit. Standard Qt example layout, vendored here so we
// can use it in pages/widgets that mix variable-width items.
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget* parent, int margin = -1, int hSpacing = -1, int vSpacing = -1);
    explicit FlowLayout(int margin = -1, int hSpacing = -1, int vSpacing = -1);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override;
    int  horizontalSpacing() const;
    int  verticalSpacing() const;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int  heightForWidth(int width) const override;
    int  count() const override;
    QLayoutItem* itemAt(int index) const override;
    QSize minimumSize() const override;
    void  setGeometry(const QRect& rect) override;
    QSize sizeHint() const override;
    QLayoutItem* takeAt(int index) override;

private:
    int doLayout(const QRect& rect, bool testOnly) const;
    int smartSpacing(QStyle::PixelMetric pm) const;

    QList<QLayoutItem*> m_items;
    int m_hSpace;
    int m_vSpace;
};

} // namespace gui
