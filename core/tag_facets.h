#pragma once
#include <core/facet_schema.h>
#include <core/load_error.h>
#include <QHash>
#include <QString>
#include <QStringList>
#include <expected>

namespace tc {

// tag_definitions.fct: one `tag=facet,facet` line per tag.
class TagFacets {
public:
    QStringList facetsFor(const QString& tag) const;
    bool isDefined(const QString& tag) const;

    // An empty facet list removes the tag.
    void set(const QString& tag, const QStringList& facets);

    // Sorted, for stable writes.
    QStringList definedTags() const;

    QStringList undefined(const QStringList& tags) const;

    qsizetype size() const;

private:
    QHash<QString, QStringList> m_map;
};

// warnings: lines with no usable definition, which a write drops.
struct TagFacetsFile {
    TagFacets defs;
    QStringList header;
    QList<LoadError> warnings;
    QString eol = QStringLiteral("\n");
    bool trailingNewline = true;
};

// Not read with readFct: a tag may start with '@'. Splits on the last '=',
// since tags can contain one ("1=2") but facets never do.
std::expected<TagFacetsFile, LoadError> readTagFacets(const QString& path);

std::expected<void, LoadError> writeTagFacets(const TagFacetsFile& file, const QString& path);

struct FacetIssue {
    QString tag;
    QString facet;
};

// Definitions using facets the schema doesn't declare.
QList<FacetIssue> unknownFacets(const TagFacets& defs, const FacetSchema& schema);

} // namespace tc
