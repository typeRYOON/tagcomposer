#pragma once
#include <QString>

namespace utils {

struct AppSettings {
    // ── Appearance ───────────────────────────────────────────────────────────
    bool    danmakuEnabled       = false;
    // Tile-view bottom gradient. start = where the fade begins as a fraction
    // of tile height (0.0 = top, 1.0 = bottom edge); alpha = 0–255 darkness
    // at the bottom edge. Defaults match the values previously hardcoded in
    // EntryView::makeTileImage. Read once at startup; mid-session changes
    // are persisted but require a restart to take effect.
    qreal   tileGradientStart    = 0.6;
    int     tileGradientAlpha    = 180;
    // Hex "#rrggbb" — colour of the tile title text. Same startup-only
    // semantics as the gradient.
    QString tileTitleColor       = "#ffffff";

    // ── ComfyUI ───────────────────────────────────────────────────────────────
    bool    comfyUiEnabled       = false;
    QString comfyUiServerAddress = "127.0.0.1:8188";
    QString comfyUiApiKey;
    QString comfyUiOutputFolder; // path pattern, e.g. C:/ComfyUI/output/{yyyy-MM-dd}
    QString comfyUiTempFolder;   // flat folder watched for in-progress decode images
    QString comfyUiInputFolder;  // optional path to ComfyUI's input/ — enables direct file copy for image vars (HTTP upload is the fallback when unset)
    QString loraBaseDir;         // base dir for LoRA relative-path computation (ComfyUI models/loras)

    // ── Facets ────────────────────────────────────────────────────────────────
    // Names of facets used by the composer's quick-add context menu.
    // character/copyright default empty — menu items are hidden when unset,
    // so users with their own facet schema aren't forced to use a baked-in
    // name. trigger_word/style default to the conventional rtrigger_word and
    // rstyle since those map directly to LoRA training/usage conventions.
    QString quickCharacterFacet;
    QString quickCopyrightFacet;
    QString quickTriggerWordFacet = "rtrigger_word";
    QString quickStyleFacet       = "rstyle";

    // ─────────────────────────────────────────────────────────────────────────
    static AppSettings load(const QString& path);
    void save(const QString& path) const;
};

} // namespace utils
