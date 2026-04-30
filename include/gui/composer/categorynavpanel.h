#pragma once
#include <QWidget>
#include <functional>

class QLabel;
class QVBoxLayout;
class QPropertyAnimation;

namespace gui {

// Floating top-right widget: a small handle that expands on hover to show
// a scrollable list of the currently visible group names.
class CategoryNavPanel : public QWidget {
public:
    std::function<void(const QString&)> onCategoryClicked;

    explicit CategoryNavPanel(QWidget* parent = nullptr);

    void updateCategories(const QStringList& displayNames);

protected:
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    QLabel*             m_handle;
    QWidget*            m_listFrame;
    QVBoxLayout*        m_listLayout;
    QPropertyAnimation* m_anim;
    int                 m_fullHeight = 0;
};

} // namespace gui
