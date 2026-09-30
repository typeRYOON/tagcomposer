#pragma once
#include <QString>

namespace tc {

inline constexpr const char* kAppName = "TagComposer";
inline constexpr const char* kAppVersion = "0.1.0";
inline constexpr const char* kAppId = "com.ryoon.TagComposer";
inline constexpr const char* kOrganizationName = "Ryoon";
inline constexpr const char* kDiscordInviteUrl = "https://discord.gg/4jgC8C9Ku8";

// Paths under the data dir. AppData resolves them; these are the names so a
// page that wants to open one in an editor does not spell it out again.
namespace paths {

inline constexpr const char* kDanbooruCsv = "system/danbooru.csv";
inline constexpr const char* kFacets = "system/facets.fct";
inline constexpr const char* kDefinitions = "system/tag_definitions.fct";
inline constexpr const char* kRules = "system/rules.fct";
inline constexpr const char* kGroups = "system/groups.fct";
inline constexpr const char* kProfiles = "system/profiles.fct";
inline constexpr const char* kVars = "system/vars.fct";
inline constexpr const char* kSettings = "system/settings.json";
inline constexpr const char* kSession = "system/session.json";
inline constexpr const char* kWorkflows = "system/workflows.json";
inline constexpr const char* kLatentSizes = "system/latent_sizes.txt";
inline constexpr const char* kClusterFilters = "system/cluster_filters.fct";
inline constexpr const char* kStatesDir = "states";
inline constexpr const char* kWorkflowInputsDir = "workflow_inputs";
inline constexpr const char* kModelsDir = "models";
inline constexpr const char* kCollectionsDir = "collections";

} // namespace paths

// Saved entry images are normalised to this size.
inline constexpr int kSaveImageWidth = 468;
inline constexpr int kSaveImageHeight = 600;

} // namespace tc
