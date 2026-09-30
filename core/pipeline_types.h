#pragma once
#include <QString>
#include <QStringList>

namespace tc {

enum class TagResult {
    Include,
    Skipped,
    Replaced,    // paired with an Injected entry carrying the new tag
    Injected,    // added by Add or Replace
    Flagged,
    NoFacets,    // no definition, rules never see it
    Deactivated, // user-muted, shown but excluded
    Deleted,     // composer drops it from the active set
};

// The results that reach the prompt. Also what "already present" means when a
// rule would inject a tag the list already carries.
constexpr bool reachesOutput(TagResult r)
{
    return r == TagResult::Include || r == TagResult::Injected || r == TagResult::NoFacets;
}

struct PipelineTag;

// The activeTags string a pipeline tag came from: its pre-expansion form when
// it has one. Weights, deactivation and custom facets all key on this, so a
// tag's weight survives a change to the variable inside it.
QString documentKey(const PipelineTag& tag);

struct PipelineTag {
    QString tag;
    QString sourceTag; // pre-variable-expansion form, empty when none
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
