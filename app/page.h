#pragma once
#include <QString>

namespace tc {

// The page stack, navbar and window title all read kPages.
enum class Page {
    Home,
    EntryViewer,
    TagComposer,
    FacetEditor,
    WorkflowEditor,
    PromptHistory,
    OutputViewer,
    DatasetHelpers,
    DanbooruWiki,
    Settings,
};

struct PageInfo {
    Page page;
    const char* title;
    const char* icon;
    bool pinBottom = false; // sits below the navbar's stretch
};

inline constexpr PageInfo kPages[] = {
    {Page::Home, "Home", ":/icons/nav_home.png"},
    {Page::EntryViewer, "Entry Viewer", ":/icons/nav_tiles.png"},
    {Page::TagComposer, "Tag Composer", ":/icons/nav_composer.png"},
    {Page::FacetEditor, "Facet Editor", ":/icons/nav_facets.png"},
    {Page::WorkflowEditor, "Workflow Editor", ":/icons/nav_workflow.png"},
    {Page::PromptHistory, "Prompt History", ":/icons/nav_history.png"},
    {Page::OutputViewer, "Output Viewer", ":/icons/nav_output.png"},
    {Page::DatasetHelpers, "Dataset Helpers", ":/icons/nav_dataset.png"},
    {Page::DanbooruWiki, "Danbooru Wiki", ":/icons/nav_wiki.png"},
    {Page::Settings, "Settings", ":/icons/nav_settings.png", true},
};

inline constexpr int kPageCount = int(std::size(kPages));

} // namespace tc
