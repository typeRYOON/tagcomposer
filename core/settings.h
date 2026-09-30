#pragma once
#include <core/load_error.h>
#include <core/prompt.h>
#include <QList>
#include <QString>
#include <expected>

namespace tc {

// settings.json. Fields without a feature yet are still carried, so a write keeps them.
struct Settings {
    // ---- Appearance
    // Tile bottom gradient: fade start (0 top, 1 bottom), alpha at the bottom edge.
    qreal tileGradientStart = 0.6;
    int tileGradientAlpha = 180;
    QString tileTitleColor = QStringLiteral("#ffffff");
    float sfxVolume = 0.5f;

    // ---- ComfyUI
    bool comfyEnabled = false;
    QString comfyServerAddress = QStringLiteral("127.0.0.1:8188");
    QString comfyApiKey;
    QString comfyOutputFolder; // may carry a date pattern, e.g. {yyyy-MM-dd}
    QString comfyTempFolder;   // watched for in-progress decode images
    QString comfyInputFolder;
    QString loraBaseDir; // "primary" root
    QString loraTestDir; // "test" root, optional
    double defaultLoraModelStrength = 1.0;
    double defaultLoraClipStrength = 1.0;

    // ---- Facets
    // Empty hides the matching quick-add menu item.
    QString quickCharacterFacet;
    QString quickCopyrightFacet;
    QString quickTriggerWordFacet;
    QString quickStyleFacet;
    QList<FacetFormat> facetFormats;

    // ---- Auto-tagger
    QString autoTagModel;
    float autoTagThreshold = 0.35f;
    int autoTagCooldownMs = 100;
    QString autoTagInputFolder;
    QString autoTagOutputFolder;
    QString tagEditorFolder;

    // ---- Composer
    bool forceOverwriteRulesOnStateLoad = false;

    // ---- Collector
    QString collectorWatchFolder;
    QString collectorActiveCollection;
    int collectorThreshold = 4; // Hamming distance cutoff
    int collectorPollSeconds = 5;

    // ---- Update checker
    qint64 lastUpdateCheckTime = 0;
    QString lastKnownLatestVersion;

    bool operator==(const Settings&) const = default;
};

// A missing file yields the defaults.
std::expected<Settings, LoadError> readSettings(const QString& path);
std::expected<void, LoadError> writeSettings(const Settings& settings, const QString& path);

} // namespace tc
