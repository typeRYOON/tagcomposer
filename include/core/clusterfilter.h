#pragma once
#include <QString>
#include <QList>

namespace core {

// Lightweight facet-based filter used by the tag-cluster page to keep or drop
// candidate tags from the Danbooru fetch results. Each rule is an AND-group of
// facet names - a tag matches a rule when its FacetIndex definition contains
// every facet listed in that rule. Multiple rules combine as OR: any rule
// matching means the tag matched the filter.
//
// `Mode` decides what "matched" means: in Blacklist the matched tags are
// dropped, in Whitelist only matched tags are kept. Tags with no facet
// definition default to "matched? = false", so blacklist keeps them and
// whitelist drops them - change ClusterFilter::keep() if you ever want to
// surface that as a user-facing toggle.
class ClusterFilter {
public:
    enum class Mode { Blacklist, Whitelist };

    Mode                          mode { Mode::Blacklist };
    // Each inner list is the AND-group; outer list is the OR.
    QList<QList<QString>>         rules;

    static ClusterFilter loadFromFile(const QString& path);
    void                 saveToFile(const QString& path) const;

    // True when `tagFacets` satisfies at least one rule (any AND-group with
    // every facet present). Empty `rules` returns false.
    bool matches(const QList<QString>& tagFacets) const;

    // True when the tag should be kept after filtering - combines `matches`
    // with `mode`.
    bool keep(const QList<QString>& tagFacets) const;
};

} // namespace core
