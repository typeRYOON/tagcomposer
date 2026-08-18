#pragma once
#include <QString>
#include <QList>

namespace utils {

// Per-facet tag wrap applied right before prompt-string assembly. Used so
// model-specific syntax (e.g. Anima's "@asanagi" for rstyle tags) can live
// in settings instead of being baked into every saved tag name.
struct FacetFormat {
    QString facet;
    QString prefix;
    QString suffix;

    // Format profiles compare whole lists to decide whether a saved state
    // still matches the profile it names.
    bool operator==(const FacetFormat& other) const = default;
};

struct AppSettings {
    // ---- Appearance
    bool danmakuEnabled = false;
    // Tile bottom gradient. start = fade-begin fraction (0=top, 1=bottom);
    // alpha = bottom-edge darkness. Read at startup only; restart to apply.
    qreal tileGradientStart = 0.6;
    int tileGradientAlpha = 180;
    QString tileTitleColor = "#ffffff"; // tile title hex; startup-only like gradient

    // ---- Sound effects
    float sfxVolume = 0.5f; // 0.0 - 1.0, applied to every clip in core::SoundPlayer

    // ---- ComfyUI
    bool comfyUiEnabled = false;
    QString comfyUiServerAddress = "127.0.0.1:8188";
    QString comfyUiApiKey;
    QString comfyUiOutputFolder; // path pattern, e.g. C:/ComfyUI/output/{yyyy-MM-dd}
    QString comfyUiTempFolder;   // flat folder watched for in-progress decode images
    QString comfyUiInputFolder;  // optional ComfyUI input/ for direct file copy
    QString loraBaseDir;         // primary lora root (ComfyUI's models/loras)
    QString loraTestDir;         // optional secondary root (e.g. ~/Downloads)
    double defaultLoraModelStr = 1.0; // applied to freshly-added LoRAs
    double defaultLoraClipStr = 1.0;

    // ---- Facets
    // Quick-add facet names. Empty = menu item hidden, so users aren't forced
    // into a baked-in schema. Settings page shows the LoRA-training convention
    // names ("rcharacter" / "rcopyright" / "rtrigger_word" / "rstyle") as hints.
    QString quickCharacterFacet;
    QString quickCopyrightFacet;
    QString quickTriggerWordFacet;
    QString quickStyleFacet;
    QList<FacetFormat> facetFormats;

    // ---- AutoTag
    QString activeAutoTagModel;
    float autoTagThreshold = 0.35f;
    int autoTagCooldownMs = 100; // 0 = no cooldown between inferences
    QString autoTagInputFolder;
    QString autoTagOutputFolder;
    QString tagEditorFolder;

    // ---- Prompt Composer
    // When loading a saved state, overwrite match expression, action (type
    // and arguments), and force-fire flag on rules already in memory (same
    // uuid). Default off keeps the local match/action/force; only enabled
    // and Add/Replace arguments are refreshed. Rule name is never overwritten
    // (rename is local to the uuid).
    bool forceOverwriteRulesOnStateLoad = false;

    // ---- Auto-collect (Collector page)
    QString collectorWatchFolder;
    QString collectorActiveCollection;
    int collectorThreshold = 4;   // Hamming bits cutoff (0-16)
    int collectorPollSeconds = 5;

    // ---- Update checker
    qint64 lastUpdateCheckTime = 0; // unix epoch seconds, throttles checks
    QString lastKnownLatestVersion;

    static AppSettings load(const QString& path);
    void save(const QString& path) const;
};

} // namespace utils
