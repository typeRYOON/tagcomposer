#include <app/composer_scroll_area.h>
#include <QKeyEvent>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QWheelEvent>

namespace tc {

ComposerScrollArea::ComposerScrollArea(QWidget* parent) : QScrollArea(parent)
{
    m_scrollAnim = new QPropertyAnimation(verticalScrollBar(), "value", this);
    m_scrollAnim->setEasingCurve(QEasingCurve::OutCubic);
}

void ComposerScrollArea::scrollToY(int y)
{
    QScrollBar* bar = verticalScrollBar();
    if (!bar) return;

    m_scrollTarget = qBound(bar->minimum(), y, bar->maximum());
    if (bar->value() == m_scrollTarget) return;

    m_scrollAnim->stop();
    m_scrollAnim->setDuration(280);
    m_scrollAnim->setStartValue(bar->value());
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
    QScrollBar* bar = verticalScrollBar();
    const int delta = event->angleDelta().y();
    if (!bar || delta == 0) {
        QScrollArea::wheelEvent(event);
        return;
    }

    // A notch is about three tag rows. The target accumulates, so spinning
    // fast compounds rather than restarting from the current position.
    constexpr int kStepPerNotch = 90;
    if (m_scrollAnim->state() != QAbstractAnimation::Running) m_scrollTarget = bar->value();
    m_scrollTarget =
        qBound(bar->minimum(), m_scrollTarget - delta * kStepPerNotch / 120, bar->maximum());

    m_scrollAnim->stop();
    m_scrollAnim->setDuration(180);
    m_scrollAnim->setStartValue(bar->value());
    m_scrollAnim->setEndValue(m_scrollTarget);
    m_scrollAnim->start();
    event->accept();
}

} // namespace tc
