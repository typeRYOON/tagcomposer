#include <core/settings.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace tc {

std::expected<Settings, LoadError> readSettings(const QString& path)
{
    Settings out;

    QFile file(path);
    if (!file.exists()) return out;
    if (!file.open(QIODevice::ReadOnly))
        return std::unexpected(LoadError{path, "cannot open: " + file.errorString()});

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return std::unexpected(
            LoadError{path, QString("invalid json at offset %1: %2")
                                .arg(parseError.offset)
                                .arg(parseError.errorString())});
    }
    if (!doc.isObject()) return std::unexpected(LoadError{path, "root is not an object"});

    const QJsonObject root = doc.object();

    const QJsonObject app = root[u"app"_s].toObject();
    out.tileGradientStart = app[u"tileGradientStart"_s].toDouble(out.tileGradientStart);
    out.tileGradientAlpha = app[u"tileGradientAlpha"_s].toInt(out.tileGradientAlpha);
    out.tileTitleColor = app[u"tileTitleColor"_s].toString(out.tileTitleColor);
    out.sfxVolume = float(app[u"sfxVolume"_s].toDouble(out.sfxVolume));

    const QJsonObject comfy = root[u"comfyui"_s].toObject();
    out.comfyEnabled = comfy[u"enabled"_s].toBool(out.comfyEnabled);
    out.comfyServerAddress = comfy[u"serverAddress"_s].toString(out.comfyServerAddress);
    out.comfyApiKey = comfy[u"apiKey"_s].toString();
    out.comfyOutputFolder = comfy[u"outputFolder"_s].toString();
    out.comfyTempFolder = comfy[u"tempFolder"_s].toString();
    out.comfyInputFolder = comfy[u"inputFolder"_s].toString();
    out.loraBaseDir = comfy[u"loraBaseDir"_s].toString();
    out.loraTestDir = comfy[u"loraTestDir"_s].toString();
    out.defaultLoraModelStrength =
        comfy[u"defaultLoraModelStr"_s].toDouble(out.defaultLoraModelStrength);
    out.defaultLoraClipStrength =
        comfy[u"defaultLoraClipStr"_s].toDouble(out.defaultLoraClipStrength);

    const QJsonObject facets = root[u"facets"_s].toObject();
    out.quickCharacterFacet = facets[u"quickCharacter"_s].toString();
    out.quickCopyrightFacet = facets[u"quickCopyright"_s].toString();
    out.quickTriggerWordFacet = facets[u"quickTriggerWord"_s].toString();
    out.quickStyleFacet = facets[u"quickStyle"_s].toString();
    for (const QJsonValue entry : facets[u"formats"_s].toArray()) {
        const QJsonObject format = entry.toObject();
        const QString facet = format[u"facet"_s].toString().trimmed();
        if (facet.isEmpty()) continue;
        out.facetFormats << FacetFormat{facet, format[u"prefix"_s].toString(),
                                        format[u"suffix"_s].toString()};
    }

    const QJsonObject autotag = root[u"autotag"_s].toObject();
    out.autoTagModel = autotag[u"activeModel"_s].toString();
    out.autoTagThreshold = float(autotag[u"threshold"_s].toDouble(out.autoTagThreshold));
    out.autoTagCooldownMs = autotag[u"cooldownMs"_s].toInt(out.autoTagCooldownMs);
    out.autoTagInputFolder = autotag[u"inputFolder"_s].toString();
    out.autoTagOutputFolder = autotag[u"outputFolder"_s].toString();
    out.tagEditorFolder = autotag[u"editorFolder"_s].toString();

    const QJsonObject composer = root[u"composer"_s].toObject();
    out.forceOverwriteRulesOnStateLoad =
        composer[u"forceOverwriteRulesOnStateLoad"_s].toBool(out.forceOverwriteRulesOnStateLoad);

    const QJsonObject collector = root[u"collector"_s].toObject();
    out.collectorWatchFolder = collector[u"watchFolder"_s].toString();
    out.collectorActiveCollection = collector[u"activeCollection"_s].toString();
    out.collectorThreshold = collector[u"threshold"_s].toInt(out.collectorThreshold);
    out.collectorPollSeconds = collector[u"pollSeconds"_s].toInt(out.collectorPollSeconds);

    const QJsonObject update = root[u"update"_s].toObject();
    out.lastUpdateCheckTime = qint64(update[u"lastCheckTime"_s].toDouble(0));
    out.lastKnownLatestVersion = update[u"lastKnownLatest"_s].toString();

    return out;
}

std::expected<void, LoadError> writeSettings(const Settings& settings, const QString& path)
{
    QJsonObject app;
    app[u"tileGradientStart"_s] = settings.tileGradientStart;
    app[u"tileGradientAlpha"_s] = settings.tileGradientAlpha;
    app[u"tileTitleColor"_s] = settings.tileTitleColor;
    app[u"sfxVolume"_s] = double(settings.sfxVolume);

    QJsonObject comfy;
    comfy[u"enabled"_s] = settings.comfyEnabled;
    comfy[u"serverAddress"_s] = settings.comfyServerAddress;
    comfy[u"apiKey"_s] = settings.comfyApiKey;
    comfy[u"outputFolder"_s] = settings.comfyOutputFolder;
    comfy[u"tempFolder"_s] = settings.comfyTempFolder;
    comfy[u"inputFolder"_s] = settings.comfyInputFolder;
    comfy[u"loraBaseDir"_s] = settings.loraBaseDir;
    comfy[u"loraTestDir"_s] = settings.loraTestDir;
    comfy[u"defaultLoraModelStr"_s] = settings.defaultLoraModelStrength;
    comfy[u"defaultLoraClipStr"_s] = settings.defaultLoraClipStrength;

    QJsonArray formats;
    for (const FacetFormat& format : settings.facetFormats) {
        QJsonObject entry;
        entry[u"facet"_s] = format.facet;
        entry[u"prefix"_s] = format.prefix;
        entry[u"suffix"_s] = format.suffix;
        formats.append(entry);
    }

    QJsonObject facets;
    facets[u"quickCharacter"_s] = settings.quickCharacterFacet;
    facets[u"quickCopyright"_s] = settings.quickCopyrightFacet;
    facets[u"quickTriggerWord"_s] = settings.quickTriggerWordFacet;
    facets[u"quickStyle"_s] = settings.quickStyleFacet;
    facets[u"formats"_s] = formats;

    QJsonObject autotag;
    autotag[u"activeModel"_s] = settings.autoTagModel;
    autotag[u"threshold"_s] = settings.autoTagThreshold;
    autotag[u"cooldownMs"_s] = settings.autoTagCooldownMs;
    autotag[u"inputFolder"_s] = settings.autoTagInputFolder;
    autotag[u"outputFolder"_s] = settings.autoTagOutputFolder;
    autotag[u"editorFolder"_s] = settings.tagEditorFolder;

    QJsonObject composer;
    composer[u"forceOverwriteRulesOnStateLoad"_s] = settings.forceOverwriteRulesOnStateLoad;

    QJsonObject collector;
    collector[u"watchFolder"_s] = settings.collectorWatchFolder;
    collector[u"activeCollection"_s] = settings.collectorActiveCollection;
    collector[u"threshold"_s] = settings.collectorThreshold;
    collector[u"pollSeconds"_s] = settings.collectorPollSeconds;

    QJsonObject update;
    update[u"lastCheckTime"_s] = double(settings.lastUpdateCheckTime);
    update[u"lastKnownLatest"_s] = settings.lastKnownLatestVersion;

    QJsonObject root;
    root[u"app"_s] = app;
    root[u"comfyui"_s] = comfy;
    root[u"facets"_s] = facets;
    root[u"autotag"_s] = autotag;
    root[u"composer"_s] = composer;
    root[u"collector"_s] = collector;
    root[u"update"_s] = update;

    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return std::unexpected(LoadError{path, "cannot open for writing: " + file.errorString()});

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit())
        return std::unexpected(LoadError{path, "write failed: " + file.errorString()});

    return {};
}

} // namespace tc
