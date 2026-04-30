#include <gui/composer/composerscrollarea.h>
#include <QKeyEvent>

namespace gui {

ComposerScrollArea::ComposerScrollArea(QWidget* parent) : QScrollArea(parent) {}

void ComposerScrollArea::keyPressEvent(QKeyEvent* event)
{
    if (event->modifiers() & Qt::ShiftModifier)
    {
        if (event->key() == Qt::Key_E) {
            emit runRequested();
        }
        else if (event->key() == Qt::Key_R) {
            if (event->modifiers() & Qt::AltModifier) {
                emit clearPendingRequested();
            }
            else {
                emit interruptRequested();
            }
        }
    }
    event->ignore();
}

} // namespace gui
