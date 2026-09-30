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

    // Sorted, so a write produces a stable diff.
    QStringList definedTags() const;

    // The subset of `tags` with no definition.
    QStringList undefined(const QStringList& tags) const;

    qsizetype size() const;

private:
    QHash<QString, QStringList> m_map;
};

// header holds the file's leading comment and blank lines verbatim.
// warnings names lines that carried no usable definition and so will not
// survive a write, a missing '=', an empty tag, or an empty facet list.
struct TagFacetsFile {
    TagFacets defs;
    QStringList header;
    QList<LoadError> warnings;
    QString eol = QStringLiteral("\n");
    bool trailingNewline = true;
};

// Flat file, not the @block grammar: a tag may start with '@' (`@character`),
// so readFct must not be used here. The tag is everything before the LAST '=',
// since a tag may contain one (`= =`, `1=2`) and a facet name never does.
std::expected<TagFacetsFile, LoadError> readTagFacets(const QString& path);

std::expected<void, LoadError> writeTagFacets(const TagFacetsFile& file, const QString& path);

struct FacetIssue {
    QString tag;
    QString facet;
};

// Definitions naming a facet the schema does not declare. The caller decides what to do.
QList<FacetIssue> unknownFacets(const TagFacets& defs, const FacetSchema& schema);

} // namespace tc
