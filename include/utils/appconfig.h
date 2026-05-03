#pragma once

namespace utils {
    inline constexpr const char* APP_NAME           = "TagComposer";
    inline constexpr const char* APP_VERSION        = "0.1.0";
    inline constexpr const char* APP_ID             = "com.ryoon.TagComposer";
    inline constexpr const char* APP_DESCRIPTION    = "Tag Composing Program";
    inline constexpr const char* ORGANIZATION_NAME  = "Ryoon";
    inline constexpr const char* DANBOORU_CSV_PATH  = "data/system/danbooru.csv";
    inline constexpr const char* DANMAKU_PATH       = "data/system/danmaku.txt";
    inline constexpr const char* FACETS_PATH        = "data/system/facets.fct";
    inline constexpr const char* DEFINITIONS_PATH   = "data/system/tag_definitions.fct";
    inline constexpr const char* RULES_PATH         = "data/system/rules.fct";
    inline constexpr const char* GROUPS_PATH        = "data/system/groups.fct";
    inline constexpr const char* VARS_PATH          = "data/system/vars.fct";
    inline constexpr const char* SETTINGS_PATH      = "data/system/settings.json";
    inline constexpr const char* SESSION_PATH       = "data/system/session.json";
    inline constexpr const char* WORKFLOWS_PATH     = "data/system/workflows.json";
    inline constexpr const char* STATES_DIR         = "data/states";
    inline constexpr const char* WORKFLOW_INPUTS_DIR = "data/workflow_inputs";
    inline constexpr const char* LATENT_SIZES_PATH  = "data/system/latent_sizes.txt";
    inline constexpr const char* BOORU_CACHE_PATH   = "data/system/global_tag_cache.json";
    inline constexpr const char* CLUSTER_FILTERS_PATH = "data/system/cluster_filters.fct";
    inline constexpr const char* MODELS_DIR             = "data/models";
    inline constexpr const char* COLLECTIONS_DIR        = "data/collections";

    inline constexpr int SAVE_IMAGE_W = 468;
    inline constexpr int SAVE_IMAGE_H = 600;

    inline QString BASE_PATH;
}
