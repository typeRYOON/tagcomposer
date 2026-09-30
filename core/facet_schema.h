#pragma once
#include <core/load_error.h>
#include <QHash>
#include <QString>
#include <QStringList>
#include <expected>

namespace tc {

// facets.fct: @category blocks and their facets, in file order.
class FacetSchema {
public:
    QString categoryFor(const QString& facet) const;
    bool hasFacet(const QString& facet) const;
    const QStringList& facets() const;
    const QStringList& categories() const;
    void addFacet(const QString& category, const QString& facet);

private:
    QHash<QString, QString> m_facetToCategory;
    QStringList m_facets;
    QStringList m_categories;
};

std::expected<FacetSchema, LoadError> readFacetSchema(const QString& path);

// Appends an @category block for facets not yet in schema, and updates schema.
std::expected<void, LoadError> appendCategory(FacetSchema& schema, const QString& path,
                                              const QString& category, const QStringList& facets);

} // namespace tc
