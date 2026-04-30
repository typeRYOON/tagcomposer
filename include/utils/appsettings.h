#pragma once
#include <QString>

namespace utils {

struct AppSettings {
    // ── Appearance ───────────────────────────────────────────────────────────
    bool    danmakuEnabled       = false;

    // ── ComfyUI ───────────────────────────────────────────────────────────────
    bool    comfyUiEnabled       = false;
    QString comfyUiServerAddress = "127.0.0.1:8188";
    QString comfyUiApiKey;
    QString comfyUiOutputFolder; // path pattern, e.g. C:/ComfyUI/output/{yyyy-MM-dd}
    QString comfyUiTempFolder;   // flat folder watched for in-progress decode images
    QString loraBaseDir;         // base dir for LoRA relative-path computation (ComfyUI models/loras)

    // ── Facets ────────────────────────────────────────────────────────────────
    // Names of facets used by the composer's quick-add context menu.
    // Empty by default — menu items are hidden when unset, so users with
    // their own facet schema aren't forced to use a baked-in name.
    QString quickCharacterFacet;
    QString quickCopyrightFacet;

    // ─────────────────────────────────────────────────────────────────────────
    static AppSettings load(const QString& path);
    void save(const QString& path) const;
};

} // namespace utils
