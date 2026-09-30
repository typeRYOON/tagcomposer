#pragma once
#include <QScrollBar>

namespace tc {

// A thin, rounded scrollbar painted by hand. The stock one cannot be made
// this shape through the stylesheet alone.
class AppScrollBar : public QScrollBar {
    Q_OBJECT

public:
    explicit AppScrollBar(Qt::Orientation orientation, QWidget* parent = nullptr);
    explicit AppScrollBar(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

} // namespace tc
