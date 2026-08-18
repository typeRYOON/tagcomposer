#include <gui/appmainwindow.h>
#include <utils/appconfig.h>
#include <utils/logger.h>
#include <core/entry.h>
#include <core/entryio.h>
#include <QApplication>
#include <QDebug>
#include <QProxyStyle>
#include <QThreadPool>
#include <QFontDatabase>
#include <QLockFile>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <windows.h>

// Time from process creation to now: the DLL loader + CRT/static init that
// runs before main(). Logged so a slow launch can be attributed to loading
// (opencv/onnxruntime/Qt) rather than to our own startup work.
static qint64 msSinceProcessStart()
{
    FILETIME creation{};
    FILETIME exitTime{};
    FILETIME kernel{};
    FILETIME user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exitTime, &kernel, &user)) return -1;
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    auto toU64 = [](const FILETIME& ft) {
        return (quint64(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    };
    return qint64((toU64(now) - toU64(creation)) / 10000); // 100ns ticks -> ms
}
#endif


class NoFocusRectStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                       const QWidget* widget) const override
    {
        if (element == PE_FrameFocusRect) return;
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
};

using namespace utils;

int main(int argc, char** argv)
{
    try {
        QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
            Qt::HighDpiScaleFactorRoundingPolicy::Floor);
        QApplication app(argc, argv);
        QLockFile lockFile(QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/" +
                           QApplication::applicationName() + ".lock");
        lockFile.setStaleLockTime(0);
        if (!lockFile.tryLock()) return 0;

        BASE_PATH = QCoreApplication::applicationDirPath();
        app.setStyle(new NoFocusRectStyle(app.style()));
        QThreadPool::globalInstance()->setMaxThreadCount(QThread::idealThreadCount());

        QFontDatabase::addApplicationFont(":/fonts/Hiragino Maru Gothic ProN W4.otf");
        QApplication::setApplicationName(QString::fromStdString(APP_NAME));
        QApplication::setOrganizationName(QString::fromStdString(ORGANIZATION_NAME));
        QApplication::setApplicationVersion(QString::fromStdString(APP_VERSION));
        QApplication::setWindowIcon(QIcon(":/icons/taskbar.png"));
        QGuiApplication::setDesktopFileName(QString::fromStdString(APP_ID));
#ifdef Q_OS_WIN
        const QString loaderLine = QString("Loader + Qt init: %1ms").arg(msSinceProcessStart());
        Logger::instance().log(loaderLine);
        // Mirrored to the message handler so the numbers are also readable
        // outside the app (QT_LOGGING_TO_CONSOLE=1, or a debugger).
        qInfo().noquote() << loaderLine;
#endif
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
