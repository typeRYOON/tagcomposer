#pragma once
#include <QList>
#include <QString>
#include <QStringList>

namespace tc {

// Facet filter for tag-cluster results. A rule is an AND-group of facets and
// the rules OR together, so "character, female" plus "copyright" keeps a tag
// that is either both of the first two or the third.
//
// A tag with no facets defined never matches any rule, which means blacklist
// keeps it and whitelist drops it. That asymmetry is the point: blacklist is
// for pruning what you have named, whitelist for keeping only what you have.
//
// Negations (written "-nsfw") run first and ignore the mode: a tag carrying a
// negated facet is dropped before the rules are consulted at all.
class ClusterFilter {
public:
    enum class Mode { Blacklist, Whitelist };

    Mode mode = Mode::Blacklist;
    QList<QStringList> rules; // outer OR, inner AND
    QStringList negations;

    static ClusterFilter load(const QString& path);
    bool save(const QString& path) const;

    bool matches(const QStringList& tagFacets) const;
    bool keep(const QStringList& tagFacets) const;
};

} // namespace tc
