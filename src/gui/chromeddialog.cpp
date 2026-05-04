#include <gui/chromeddialog.h>
#include <gui/widgets/windowchrome.h>
#include <utils/qutils.h>
#include <QEvent>
#include <QKeyEvent>
#include <QPropertyAnimation>
#include <QShowEvent>
#include <QVBoxLayout>

namespace gui {

ChromedDialog::ChromedDialog(QWidget* parent) : QDialog(parent)
{
    // Frameless = no native chrome. The Dialog flag preserves QDialog's
    // modality & exec() semantics; FramelessWindowHint strips the OS frame.
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setObjectName("ChromedDialog");
    setAttribute(Qt::WA_StyledBackground, true);

    // Modal dialogs need close-only chrome and the modal mouse-grab fix
    // (see WindowChrome::beginResizeDrag for why exec() needs that).
    WindowChrome::Options opt;
    opt.showMin = false;
    opt.showMax = false;
    opt.showClose = true;
    opt.modalGrab = true;
    m_chrome = new WindowChrome(this, opt);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(m_chrome->frame());

    // Fade in/out on show/close - matches AppMainWindow & PreviewPopoutWindow.
    // The first showEvent transitions opacity from 0 → 1; done() (covering
    // accept/reject and the default closeEvent) fades 1 → 0 then defers to
    // QDialog::done() so exec() returns only after the animation finishes.
    setWindowOpacity(0.0);
}

QWidget* ChromedDialog::contentArea() const
{
    return m_chrome->bodyWidget();
}

void ChromedDialog::changeEvent(QEvent* event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange && m_chrome) m_chrome->onWindowStateChanged();
}

void ChromedDialog::keyPressEvent(QKeyEvent* event)
{
    // Esc only fires here if no focused child consumed it. While fullscreen,
    // catch it for "exit fullscreen" instead of letting QDialog close - the
    // titlebar isn't visible in fullscreen so otherwise the user has no way
    // out short of F11.
    if (event->key() == Qt::Key_Escape && isFullScreen()) {
        showNormal();
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void ChromedDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    // exec() may show the same dialog repeatedly, so reset opacity each time.
    // The < 0.99 guard prevents stacking animations if the dialog is briefly
    // re-shown while still fading in.
    if (windowOpacity() < 0.99) {
        utils::propertyAnimate(this, "windowOpacity", windowOpacity(), 1.0, 200,
                               QEasingCurve::InOutSine);
    }
}

void ChromedDialog::done(int result)
{
    // Re-entrant guard: once the fade-out animation finishes it calls back
    // into done() to actually close the dialog - the second call should fall
    // straight through to QDialog::done() instead of starting another fade.
    if (m_isClosing) {
        QDialog::done(result);
        return;
    }
    m_isClosing = true;

    auto* anim = utils::propertyAnimate(this, "windowOpacity", windowOpacity(), 0.0, 200,
                                        QEasingCurve::InOutSine);
    connect(anim, &QPropertyAnimation::finished, this, [this, result]() {
        QDialog::done(result);
        // Reset state so exec()-twice (re-show after dismiss) starts cleanly.
        m_isClosing = false;
        setWindowOpacity(0.0);
    });
}

} // namespace gui
