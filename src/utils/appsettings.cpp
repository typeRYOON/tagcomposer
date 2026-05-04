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
    if (!f.open(QIODevice::ReadOnly)) return s;

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
    s.comfyUiEnabled = cui["enabled"].toBool(false);
    s.comfyUiServerAddress = cui["serverAddress"].toString("127.0.0.1:8188");
    s.comfyUiApiKey = cui["apiKey"].toString();
    s.comfyUiOutputFolder = cui["outputFolder"].toString();
    s.comfyUiTempFolder = cui["tempFolder"].toString();
    s.comfyUiInputFolder = cui["inputFolder"].toString();
    s.loraBaseDir = cui["loraBaseDir"].toString();

    const QJsonObject facets = root["facets"].toObject();
    s.quickCharacterFacet = facets["quickCharacter"].toString();
    s.quickCopyrightFacet = facets["quickCopyright"].toString();
    s.quickTriggerWordFacet = facets["quickTriggerWord"].toString();
    s.quickStyleFacet = facets["quickStyle"].toString();

    const QJsonObject autotag = root["autotag"].toObject();
    s.activeAutoTagModel = autotag["activeModel"].toString();
    if (autotag.contains("threshold"))
        s.autoTagThreshold = float(autotag["threshold"].toDouble(0.35));
    if (autotag.contains("cooldownMs")) s.autoTagCooldownMs = autotag["cooldownMs"].toInt(100);
    s.autoTagInputFolder = autotag["inputFolder"].toString();
    s.autoTagOutputFolder = autotag["outputFolder"].toString();
    s.tagEditorFolder = autotag["editorFolder"].toString();

    const QJsonObject collector = root["collector"].toObject();
    s.collectorWatchFolder = collector["watchFolder"].toString();
    s.collectorActiveCollection = collector["activeCollection"].toString();
    if (collector.contains("threshold")) s.collectorThreshold = collector["threshold"].toInt(4);
    if (collector.contains("pollSeconds"))
        s.collectorPollSeconds = collector["pollSeconds"].toInt(5);

    const QJsonObject upd = root["update"].toObject();
    s.lastUpdateCheckTime = qint64(upd["lastCheckTime"].toDouble(0));
    s.lastKnownLatestVersion = upd["lastKnownLatest"].toString();

    return s;
}

void AppSettings::save(const QString& path) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonObject app;
    app["danmaku"] = danmakuEnabled;
    app["tileGradientStart"] = tileGradientStart;
    app["tileGradientAlpha"] = tileGradientAlpha;
    app["tileTitleColor"] = tileTitleColor;

    QJsonObject cui;
    cui["enabled"] = comfyUiEnabled;
    cui["serverAddress"] = comfyUiServerAddress;
    cui["apiKey"] = comfyUiApiKey;
    cui["outputFolder"] = comfyUiOutputFolder;
    cui["tempFolder"] = comfyUiTempFolder;
    cui["inputFolder"] = comfyUiInputFolder;
    cui["loraBaseDir"] = loraBaseDir;

    QJsonObject facets;
    facets["quickCharacter"] = quickCharacterFacet;
    facets["quickCopyright"] = quickCopyrightFacet;
    facets["quickTriggerWord"] = quickTriggerWordFacet;
    facets["quickStyle"] = quickStyleFacet;

    QJsonObject autotag;
    autotag["activeModel"] = activeAutoTagModel;
    autotag["threshold"] = autoTagThreshold;
    autotag["cooldownMs"] = autoTagCooldownMs;
    autotag["inputFolder"] = autoTagInputFolder;
    autotag["outputFolder"] = autoTagOutputFolder;
    autotag["editorFolder"] = tagEditorFolder;

    QJsonObject collector;
    collector["watchFolder"] = collectorWatchFolder;
    collector["activeCollection"] = collectorActiveCollection;
    collector["threshold"] = collectorThreshold;
    collector["pollSeconds"] = collectorPollSeconds;

    QJsonObject upd;
    // QJsonValue stores numbers as double - fine for unix timestamps until
    // the year 287396 or so. No need for the hex-string trick we use for
    // 64-bit hashes.
    upd["lastCheckTime"] = double(lastUpdateCheckTime);
    upd["lastKnownLatest"] = lastKnownLatestVersion;

    QJsonObject root;
    root["app"] = app;
    root["comfyui"] = cui;
    root["facets"] = facets;
    root["autotag"] = autotag;
    root["collector"] = collector;
    root["update"] = upd;

    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

} // namespace utils
