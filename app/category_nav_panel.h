#pragma once
#include <QHash>
#include <QStringList>
#include <QWidget>
#include <functional>

class QLabel;
class QPropertyAnimation;
class QVBoxLayout;

namespace tc {

// Hover-expanding list of the groups on screen; a click scrolls to one.
class CategoryNavPanel : public QWidget {
    Q_OBJECT

public:
    explicit CategoryNavPanel(QWidget* parent = nullptr);

    std::function<void(const QString&)> onCategoryClicked;

    // undefinedCounts: per group, tags without facets. Nonzero counts warn.
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
