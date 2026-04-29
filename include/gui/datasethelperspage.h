#pragma once
#include <QWidget>
#include <QStackedWidget>
#include <QPushButton>
#include <QButtonGroup>
#include <QVBoxLayout>

namespace gui {

class DatasetHelpersPage : public QWidget {
    Q_OBJECT
public:
    explicit DatasetHelpersPage(QWidget* parent = nullptr);

private:
    QVBoxLayout*    m_tabLayout;
    QStackedWidget* m_stack;
    QButtonGroup*   m_tabGroup;

    void addTab(const QString& label, QWidget* page);
};

} // namespace gui
