#include <gui/composer/composerscrollarea.h>
#include <QKeyEvent>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QWheelEvent>

namespace gui {

ComposerScrollArea::ComposerScrollArea(QWidget* parent) : QScrollArea(parent)
{
    m_scrollAnim = new QPropertyAnimation(verticalScrollBar(), "value", this);
    m_scrollAnim->setEasingCurve(QEasingCurve::OutCubic);
}

void ComposerScrollArea::scrollToY(int y)
{
    QScrollBar* sb = verticalScrollBar();
    if (!sb) return;
    m_scrollTarget = qBound(sb->minimum(), y, sb->maximum());
    if (sb->value() == m_scrollTarget) return;

    m_scrollAnim->stop();
    m_scrollAnim->setDuration(280);
    m_scrollAnim->setStartValue(sb->value());
    m_scrollAnim->setEndValue(m_scrollTarget);
    m_scrollAnim->start();
}

void ComposerScrollArea::keyPressEvent(QKeyEvent* event)
{
    if (event->modifiers() & Qt::ShiftModifier) {
        if (event->key() == Qt::Key_E) {
            emit runRequested();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_R) {
            if (event->modifiers() & Qt::AltModifier)
                emit clearPendingRequested();
            else
                emit interruptRequested();
            event->accept();
            return;
        }
    }
    QScrollArea::keyPressEvent(event);
}

void ComposerScrollArea::wheelEvent(QWheelEvent* event)
{
    QScrollBar* sb = verticalScrollBar();
    const int delta = event->angleDelta().y();
    if (!sb || delta == 0) {
        QScrollArea::wheelEvent(event);
        return;
    }

    // One notch = ~3 tag rows. Accumulate into the live target so rapid
    // spins compound rather than restart from current position.
    constexpr int kStepPerNotch = 90;
    if (m_scrollAnim->state() != QAbstractAnimation::Running) m_scrollTarget = sb->value();
    m_scrollTarget = qBound(sb->minimum(), m_scrollTarget - delta * kStepPerNotch / 120,
                            sb->maximum());

    m_scrollAnim->stop();
    m_scrollAnim->setDuration(180);
    m_scrollAnim->setStartValue(sb->value());
    m_scrollAnim->setEndValue(m_scrollTarget);
    m_scrollAnim->start();
    event->accept();
}

} // namespace gui
