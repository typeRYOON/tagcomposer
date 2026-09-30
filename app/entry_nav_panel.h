#pragma once
#include <QString>
#include <QWidget>
#include <functional>

class QLabel;
class QPropertyAnimation;
class QVBoxLayout;

namespace tc {

// Hover-expanding list of entries pushed to the composer; a click scrolls the
// grid to one. Mirrors CategoryNavPanel.
class EntryNavPanel : public QWidget {
    Q_OBJECT

public:
    explicit EntryNavPanel(QWidget* parent = nullptr);

    std::function<void(int)> onEntryClicked;

    // (title, grid index) pairs.
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
