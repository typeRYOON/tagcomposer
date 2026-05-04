#pragma once
#include <QString>
#include <QList>
#include <QHash>

namespace core {

class FacetIndex {
public:
    // schemaPath: @category blocks only - never rewritten by the app
    static FacetIndex loadFromFile(const QString& schemaPath);

    // Re-reads the schema from disk into this instance, replacing categories
    // and facet definitions but leaving tag → facet mappings untouched.
    void reloadSchemaFromFile(const QString& schemaPath);

    // definitionsPath: tag=facet lines - loaded separately, saved at shutdown
    void loadDefinitionsFromFile(const QString& definitionsPath);
    void saveDefinitions(const QString& definitionsPath) const;

    // Tag lookups
    QList<QString> facetsFor(const QString& tag) const;
    bool hasFacets(const QString& tag) const;

    // In-memory update - persisted via saveDefinitions at shutdown
    void setDefinition(const QString& tag, const QList<QString>& facets);

    // Facet lookups
    QString categoryFor(const QString& facet) const;

    // Enumeration
    QList<QString> allCategories() const; // in definition order
    QList<QString> allFacets() const;
    QList<QString> allDefinedTags() const;

    // Returns tags from the given list that have no definition
    QList<QString> undefined(const QList<QString>& tags) const;

private:
    QHash<QString, QList<QString>> m_tagToFacets;
    QHash<QString, QString> m_facetToCategory;
    QList<QString> m_categories; // insertion-ordered
    QList<QString> m_facetList;  // insertion-ordered
};

} // namespace core
