#pragma once
#include <QString>
#include <QList>

namespace core {

// Facet-based filter for tag-cluster results. Each rule is an AND-group of
// facets; rules combine as OR. Tags with no facet definition never match,
// so blacklist keeps them and whitelist drops them.
class ClusterFilter {
public:
    enum class Mode { Blacklist, Whitelist };

    Mode mode{Mode::Blacklist};
    QList<QList<QString>> rules; // outer = OR, inner = AND

    static ClusterFilter loadFromFile(const QString& path);
    void saveToFile(const QString& path) const;

    bool matches(const QList<QString>& tagFacets) const;
    bool keep(const QList<QString>& tagFacets) const;
};

} // namespace core
