#pragma once
#include <QString>
#include <QWidget>
#include <functional>

class QLabel;
class QPropertyAnimation;
class QVBoxLayout;

namespace tc {

// The float in the entry grid's top right: a handle that expands on hover
// into the list of entries currently pushed to the composer. Clicking one
// scrolls the grid to it.
//
// The same shape as the composer's CategoryNavPanel, but listing entries
// rather than groups, and with its own styling.
class EntryNavPanel : public QWidget {
    Q_OBJECT

public:
    explicit EntryNavPanel(QWidget* parent = nullptr);

    std::function<void(int)> onEntryClicked;

    // Each item is a display title and the grid index to scroll to.
    void setEntries(const QList<QPair<QString, int>>& items);

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QLabel* m_handle = nullptr;
    QWidget* m_listFrame = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
    QPropertyAnimation* m_animation = nullptr;
    int m_fullHeight = 0;
};

} // namespace tc
