#pragma once
#include <core/load_error.h>
#include <core/prompt.h>
#include <QList>
#include <QString>
#include <expected>

namespace tc {

// settings.json, in full. Every field is carried even where the feature that
// uses it is not ported yet: the file is shared with the old app, and writing
// back a subset would erase the rest.
struct Settings {
    // ---- Appearance
    // Tile bottom gradient. start is where the fade begins (0 top, 1 bottom),
    // alpha is the darkness at the bottom edge.
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
    // Empty means the matching quick-add menu item stays hidden, so nobody is
    // forced into a particular facet naming scheme.
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

// A missing file is not an error: first run has no settings.json, and the
// defaults above are the right answer.
std::expected<Settings, LoadError> readSettings(const QString& path);
std::expected<void, LoadError> writeSettings(const Settings& settings, const QString& path);

} // namespace tc
