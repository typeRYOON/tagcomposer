#include <app/chromed_dialog.h>
#include <app/widget_utils.h>
#include <app/window_chrome.h>
#include <QDialogButtonBox>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QPropertyAnimation>
#include <QShowEvent>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace tc {

ChromedDialog::ChromedDialog(QWidget* parent) : QDialog(parent)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setObjectName(u"ChromedDialog"_s);
    setAttribute(Qt::WA_StyledBackground, true);

    WindowChrome::Options options;
    options.showMin = false;
    options.showMax = false;
    options.showClose = true;
    options.modalGrab = true;
    m_chrome = new WindowChrome(this, options);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(m_chrome->frame());

    setWindowOpacity(0.0);
}

QWidget* ChromedDialog::contentArea() const
{
    return m_chrome->body();
}

bool ChromedDialog::confirm(QWidget* parent, const QString& title, const QString& message,
                            const QString& confirmText, const QString& cancelText)
{
    ChromedDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setMinimumWidth(380);

    auto* layout = new QVBoxLayout(dialog.contentArea());
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(12);

    auto* label = new QLabel(message);
    label->setWordWrap(true);
    layout->addWidget(label);
    layout->addStretch();

    auto* buttons = new QDialogButtonBox;
    buttons->addButton(confirmText, QDialogButtonBox::AcceptRole);
    buttons->addButton(cancelText, QDialogButtonBox::RejectRole);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    return dialog.exec() == QDialog::Accepted;
}

void ChromedDialog::changeEvent(QEvent* event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange && m_chrome) m_chrome->onWindowStateChanged();
}

void ChromedDialog::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && isFullScreen()) {
        QPropertyAnimation* out = propertyAnimate(this, "windowOpacity", windowOpacity(), 0.0,
                                                  200, QEasingCurve::InOutSine);
        connect(out, &QPropertyAnimation::finished, this, [this]() {
            showNormal();
            propertyAnimate(this, "windowOpacity", windowOpacity(), 1.0, 200,
                            QEasingCurve::InOutSine);
        });
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void ChromedDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    if (windowOpacity() < 0.99)
        propertyAnimate(this, "windowOpacity", windowOpacity(), 1.0, 200,
                        QEasingCurve::InOutSine);
}

void ChromedDialog::done(int result)
{
    if (m_closing) {
        QDialog::done(result);
        return;
    }
    m_closing = true;

    QPropertyAnimation* out =
        propertyAnimate(this, "windowOpacity", windowOpacity(), 0.0, 200, QEasingCurve::InOutSine);
    connect(out, &QPropertyAnimation::finished, this, [this, result]() {
        QDialog::done(result);
        m_closing = false;
        setWindowOpacity(0.0);
    });
}

} // namespace tc
