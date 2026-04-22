#include <gui/appmainwindow.h>
#include <utils/appconfig.h>
#include <core/entry.h>
#include <io/entryio.h>
#include <QApplication>
#include <QThreadPool>
#include <iostream>
#include <QFontDatabase>


using namespace utils;

int32_t main(int32_t argc, char** argv)
{
    try {
        QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
            Qt::HighDpiScaleFactorRoundingPolicy::Floor
        );
        QThreadPool::globalInstance()->setMaxThreadCount(QThread::idealThreadCount());

        QApplication app(argc, argv);
        BASE_PATH = QCoreApplication::applicationDirPath();

        QApplication::setApplicationName(QString::fromStdString(APP_NAME));
        QApplication::setOrganizationName(QString::fromStdString(ORGANIZATION_NAME));
        QApplication::setApplicationVersion(QString::fromStdString(APP_VERSION));
        //QApplication::setWindowIcon(QIcon(":/icons/app_icon.ico"));
        QGuiApplication::setDesktopFileName(QString::fromStdString(APP_ID));

        gui::AppMainWindow window;

        // set theming here.
        QFontDatabase::addApplicationFont(":/system/Hiragino Maru Gothic ProN W4.otf"); // todo, actual font loader.

        return QApplication::exec();
    }
    catch (const std::exception& e) {
        std::cerr << "UNCAUGHT EXCEPTION: " << e.what() << '\n';
        return 1;
    }
    catch (...) {
        std::cerr << "UNKNOWN EXCEPTION\n";
        return 1;
    }

}
