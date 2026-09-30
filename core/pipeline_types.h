#pragma once
#include <QString>
#include <QStringList>

namespace tc {

enum class TagResult {
    Include,
    Skipped,
    Replaced,    // paired with an Injected tag
    Injected,    // added by Add or Replace
    Flagged,
    NoFacets,    // no definition, rules never see it
    Deactivated, // user-muted, shown but excluded
    Deleted,     // composer drops it from the active set
};

// Results that reach the prompt.
constexpr bool reachesOutput(TagResult r)
{
    return r == TagResult::Include || r == TagResult::Injected || r == TagResult::NoFacets;
}

struct PipelineTag;

// The activeTags key: the pre-expansion text when there is one.
QString documentKey(const PipelineTag& tag);

struct PipelineTag {
    QString tag;
    QString sourceTag; // pre-expansion text, or empty
    QStringList facets;
    TagResult result = TagResult::Include;
    QString ruleSource;
    QString flagLabel;
    float weight = 1.0f;

    bool operator==(const PipelineTag&) const = default;
};

inline QString documentKey(const PipelineTag& tag)
{
    return tag.sourceTag.isEmpty() ? tag.tag : tag.sourceTag;
}

} // namespace tc
