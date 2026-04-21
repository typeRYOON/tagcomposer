#include <tagcomposer/gui/tagcomposer.h>
#include <tagcomposer/utils/utils.h>
#include <QApplication>
#include <QTimer>

namespace tagcomposer {


AppMainWindow::AppMainWindow(QWidget* parent)
    : QMainWindow{ parent }, m_appPath{ QCoreApplication::applicationDirPath() }
{

    setWindowOpacity(0.0);
    setMinimumSize(1280, 1280);

    QTimer::singleShot(500, this, [this]() {
        show();
        propertyAnimate(this, "windowOpacity", 0.0, 1.0, 500, QEasingCurve::InOutSine);
    });
}




void AppMainWindow::closeEvent(QCloseEvent* event)
{
    static bool isClosing{ false };
    if (isClosing) {
        event->accept();
        return;
    }
    event->ignore();
    qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
    isClosing = true;

    connect(
        propertyAnimate(this, "windowOpacity", 1.0, 0.0, 500, QEasingCurve::InOutSine),
        &QPropertyAnimation::finished,
        this,
        []() { qApp->quit(); }
    );
}

}



