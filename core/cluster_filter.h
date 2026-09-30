#pragma once
#include <QList>
#include <QString>
#include <QStringList>

namespace tc {

// Facet filter for tag-cluster results: rules are AND-groups of facets, OR'd
// together. Tags without facets never match, so blacklist keeps them and
// whitelist drops them. Negations ("-nsfw") drop a tag regardless of mode.
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
