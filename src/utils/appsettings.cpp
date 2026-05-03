#include <utils/appsettings.h>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>

namespace utils {

AppSettings AppSettings::load(const QString& path)
{
    AppSettings s;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return s;

    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();

    const QJsonObject app = root["app"].toObject();
    s.danmakuEnabled = app["danmaku"].toBool(false);
    if (app.contains("tileGradientStart"))
        s.tileGradientStart = app["tileGradientStart"].toDouble(0.6);
    if (app.contains("tileGradientAlpha"))
        s.tileGradientAlpha = app["tileGradientAlpha"].toInt(180);
    if (app.contains("tileTitleColor"))
        s.tileTitleColor = app["tileTitleColor"].toString("#ffffff");

    const QJsonObject cui = root["comfyui"].toObject();
    s.comfyUiEnabled       = cui["enabled"].toBool(false);
    s.comfyUiServerAddress = cui["serverAddress"].toString("127.0.0.1:8188");
    s.comfyUiApiKey        = cui["apiKey"].toString();
    s.comfyUiOutputFolder  = cui["outputFolder"].toString();
    s.comfyUiTempFolder    = cui["tempFolder"].toString();
    s.comfyUiInputFolder   = cui["inputFolder"].toString();
    s.loraBaseDir          = cui["loraBaseDir"].toString();

    const QJsonObject facets = root["facets"].toObject();
    s.quickCharacterFacet = facets["quickCharacter"].toString();
    s.quickCopyrightFacet = facets["quickCopyright"].toString();
    // For the two new fields, missing key → struct default; present key (even
    // empty) → user override, so users can explicitly disable a quick-add.
    if (facets.contains("quickTriggerWord"))
        s.quickTriggerWordFacet = facets["quickTriggerWord"].toString();
    if (facets.contains("quickStyle"))
        s.quickStyleFacet = facets["quickStyle"].toString();

    return s;
}

void AppSettings::save(const QString& path) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonObject app;
    app["danmaku"]           = danmakuEnabled;
    app["tileGradientStart"] = tileGradientStart;
    app["tileGradientAlpha"] = tileGradientAlpha;
    app["tileTitleColor"]    = tileTitleColor;

    QJsonObject cui;
    cui["enabled"]       = comfyUiEnabled;
    cui["serverAddress"] = comfyUiServerAddress;
    cui["apiKey"]        = comfyUiApiKey;
    cui["outputFolder"]  = comfyUiOutputFolder;
    cui["tempFolder"]    = comfyUiTempFolder;
    cui["inputFolder"]   = comfyUiInputFolder;
    cui["loraBaseDir"]   = loraBaseDir;

    QJsonObject facets;
    facets["quickCharacter"]   = quickCharacterFacet;
    facets["quickCopyright"]   = quickCopyrightFacet;
    facets["quickTriggerWord"] = quickTriggerWordFacet;
    facets["quickStyle"]       = quickStyleFacet;

    QJsonObject root;
    root["app"]    = app;
    root["comfyui"] = cui;
    root["facets"] = facets;

    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

} // namespace utils
