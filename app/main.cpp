#include <app/app_window.h>
#include <QApplication>
#include <QStyleOption>
#include <QPainter>
#include <QProxyStyle>
#include <QGuiApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFontDatabase>
#include <QIcon>

using namespace Qt::StringLiterals;

namespace {

// Registers the bundled font as the app default; the stylesheets rely on it.
void loadApplicationFont()
{
    const int id = QFontDatabase::addApplicationFont(
        QStringLiteral(":/fonts/Hiragino Maru Gothic ProN W4.otf"));
    if (id < 0) {
        qWarning("application font failed to load; the UI will use a fallback");
        return;
    }

    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    if (families.isEmpty()) return;

    QFont font = QApplication::font();
    font.setFamily(families.first());
    QApplication::setFont(font);
}

// Every .qss under :/styles, sorted so app.qss comes first as the base.
QString loadStyleSheet()
{
    QStringList paths;
    QDirIterator it(u":/styles"_s, {u"*.qss"_s}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        paths << it.next();
    paths.sort();

    if (paths.isEmpty()) {
        qWarning("no stylesheets in resources; did the .qrc compile?");
        return {};
    }

    QString combined;
    for (const QString& path : paths) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) continue;
        combined += QString::fromUtf8(file.readAll());
        combined += u'\n';
    }
    return combined;
}

} // namespace

// Drops the dotted focus rectangle, which stylesheets can't reach.
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

int main(int argc, char** argv)
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::Floor);

    QApplication app(argc, argv);
    app.setStyle(new NoFocusRectStyle(app.style()));
    app.setWindowIcon(QIcon(u":/icons/icon.ico"_s));
    loadApplicationFont();
    app.setStyleSheet(loadStyleSheet());

    // Default to data/ beside the executable, not the working directory.
    const QStringList args = QApplication::arguments();
    const QString dataDir =
        args.size() >= 2 ? args[1] : QApplication::applicationDirPath() + u"/data"_s;

    if (!QDir(dataDir + u"/system"_s).exists())
        qWarning("no system/ under %s; pass the data dir as the first argument",
                 qUtf8Printable(dataDir));

    tc::AppWindow window(dataDir);
    window.show();

    return app.exec();
}
