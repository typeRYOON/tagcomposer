#pragma once
#include <QHash>
#include <QStringList>
#include <QWidget>
#include <functional>

class QLabel;
class QPropertyAnimation;
class QVBoxLayout;

namespace tc {

// The float in the composer's top right: a small handle that expands on
// hover into the list of group names currently on screen.
class CategoryNavPanel : public QWidget {
    Q_OBJECT

public:
    explicit CategoryNavPanel(QWidget* parent = nullptr);

    std::function<void(const QString&)> onCategoryClicked;

    // `undefinedCounts` maps a display name to how many of its tags have no
    // facet definition. A positive count adds a suffix and the warn property,
    // and the handle itself warns when any count is positive.
    void updateCategories(const QStringList& displayNames,
                          const QHash<QString, int>& undefinedCounts = {});

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void setHandleWarn(bool warn);

    QLabel* m_handle = nullptr;
    QLabel* m_handleIcon = nullptr;
    QWidget* m_listFrame = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
    QPropertyAnimation* m_animation = nullptr;
    int m_fullHeight = 0;
};

} // namespace tc
