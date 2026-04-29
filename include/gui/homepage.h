#pragma once
#include <QWidget>

namespace gui {

class HomePage : public QWidget {
    Q_OBJECT
public:
    explicit HomePage(QWidget* parent = nullptr);
};

} // namespace gui
