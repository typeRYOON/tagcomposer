#pragma once
#include <QScrollArea>

class QPropertyAnimation;
class QWheelEvent;

namespace tc {

// The tag list viewport: animated scrolling, plus the run shortcuts while it
// has focus.
class ComposerScrollArea : public QScrollArea {
    Q_OBJECT

public:
    explicit ComposerScrollArea(QWidget* parent = nullptr);

    // Animated scroll to a y inside the inner widget.
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
