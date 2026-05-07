#pragma once
#include <QString>
#include <QList>
#include <QHash>

namespace core {

// schema (facets.fct): @category blocks; never rewritten.
// definitions (definitions.fct): tag=facet,facet pairs; rewritten by saveDefinitions.
class FacetIndex {
public:
    static FacetIndex loadFromFile(const QString& schemaPath);

    // Replaces categories and facets; leaves tag -> facet mappings alone.
    void reloadSchemaFromFile(const QString& schemaPath);

    void loadDefinitionsFromFile(const QString& definitionsPath);
    void saveDefinitions(const QString& definitionsPath) const;

    QList<QString> facetsFor(const QString& tag) const;
    bool hasFacets(const QString& tag) const;
    void setDefinition(const QString& tag, const QList<QString>& facets);

    // Appends a new "@category" block to schemaPath listing only facets that
    // aren't already known. Updates in-memory state. No-op if all already
    // exist or the file can't be opened.
    void appendFacets(const QString& schemaPath, const QString& category,
                      const QStringList& facets);

    QString categoryFor(const QString& facet) const;

    QList<QString> allCategories() const;
    QList<QString> allFacets() const;
    QList<QString> allDefinedTags() const;

    QList<QString> undefined(const QList<QString>& tags) const;

private:
    QHash<QString, QList<QString>> m_tagToFacets;
    QHash<QString, QString> m_facetToCategory;
    QList<QString> m_categories; // insertion-ordered
    QList<QString> m_facetList;  // insertion-ordered
};

} // namespace core
