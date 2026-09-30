#pragma once
#include <QScrollArea>

class QPropertyAnimation;
class QWheelEvent;

namespace tc {

// The composer's tag list viewport. Scrolling is animated rather than
// instant, and the run shortcuts are caught here because this is what holds
// focus while the list is being worked through.
class ComposerScrollArea : public QScrollArea {
    Q_OBJECT

public:
    explicit ComposerScrollArea(QWidget* parent = nullptr);

    // Animated jump to an absolute y inside the inner widget, so a category
    // nav click glides instead of snapping.
    void scrollToY(int y);

signals:
    void runRequested();
    void interruptRequested();
    void clearPendingRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    QPropertyAnimation* m_scrollAnim = nullptr;
    int m_scrollTarget = 0;
};

} // namespace tc
