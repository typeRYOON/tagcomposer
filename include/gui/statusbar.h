#pragma once
#include <QWidget>
#include <QLabel>

namespace gui {

class StatusBar : public QWidget {
    Q_OBJECT
public:
    explicit StatusBar(QWidget* parent = nullptr);

public slots:
    void showMessage(const QString& message);

private:
    QLabel* m_label;
};

} // namespace gui
