#include <gui/appmainwindow.h>
#include <utils/appconfig.h>
#include <core/entry.h>
#include <core/entryio.h>
#include <QApplication>
#include <QProxyStyle>
#include <QThreadPool>
#include <QFontDatabase>
#include <QLockFile>
#include <QStandardPaths>


class NoFocusRectStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;
    void drawPrimitive(
        PrimitiveElement element,
        const QStyleOption* option,
        QPainter* painter,
        const QWidget* widget) const override
    {
        if (element == PE_FrameFocusRect)
            return;
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
};

using namespace utils;

int32_t main(int32_t argc, char** argv)
{
    try {
        QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
            Qt::HighDpiScaleFactorRoundingPolicy::Floor
        );
        QApplication app(argc, argv);
        QLockFile lockFile(
            QStandardPaths::writableLocation(QStandardPaths::TempLocation)
            + "/" + QApplication::applicationName() + ".lock"
        );
        lockFile.setStaleLockTime(0);
        if (!lockFile.tryLock())
            return 0;

        BASE_PATH = QCoreApplication::applicationDirPath();
        app.setStyle(new NoFocusRectStyle(app.style()));
        QThreadPool::globalInstance()->setMaxThreadCount(QThread::idealThreadCount());

        QFontDatabase::addApplicationFont(":/system/Hiragino Maru Gothic ProN W4.otf");
        QApplication::setApplicationName(QString::fromStdString(APP_NAME));
        QApplication::setOrganizationName(QString::fromStdString(ORGANIZATION_NAME));
        QApplication::setApplicationVersion(QString::fromStdString(APP_VERSION));
        QApplication::setWindowIcon(QIcon(":/icons/taskbar.png"));
        QGuiApplication::setDesktopFileName(QString::fromStdString(APP_ID));
        gui::AppMainWindow window;

        return QApplication::exec();
    }
    catch (const std::exception& e) {
        qDebug() << "UNCAUGHT EXCEPTION: " << e.what() << '\n';
        return 1;
    }
    catch (...) {
        qDebug() << "UNKNOWN EXCEPTION\n";
        return 1;
    }

}
