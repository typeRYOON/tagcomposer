#pragma once
#include <QScrollBar>

namespace gui {

class AppScrollBar : public QScrollBar {
    Q_OBJECT
public:
    explicit AppScrollBar(Qt::Orientation orientation, QWidget* parent = nullptr);
    explicit AppScrollBar(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

} // namespace gui
