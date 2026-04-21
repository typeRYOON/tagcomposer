#include <tagcomposer/gui/tagcomposer.h>
#include <tagcomposer/utils/appconstants.h>
#include <QApplication>
#include <QThreadPool>
#include <iostream>


int32_t main(int32_t argc, char** argv)
{
    try {
        QThreadPool::globalInstance()->setMaxThreadCount(QThread::idealThreadCount());
        QApplication app(argc, argv);
        QApplication::setApplicationName(QString::fromStdString(APP_NAME));
        QApplication::setOrganizationName(QString::fromStdString(ORGANIZATION_NAME));
        QApplication::setApplicationVersion(QString::fromStdString(APP_VERSION));
        QApplication::setWindowIcon(QIcon(":/icons/app_icon.ico"));
        QGuiApplication::setDesktopFileName(QString::fromStdString(APP_ID));

        tagcomposer::AppMainWindow window;

        // set theming here.

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
