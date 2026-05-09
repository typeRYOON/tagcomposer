#pragma once
#include <QScrollArea>

class QPropertyAnimation;
class QWheelEvent;

namespace gui {

class ComposerScrollArea : public QScrollArea {
    Q_OBJECT
public:
    explicit ComposerScrollArea(QWidget* parent = nullptr);

    // Animated jump to an absolute Y inside the inner widget. Used by the
    // category nav so clicks glide instead of snapping.
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

} // namespace gui
