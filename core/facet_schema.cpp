#include <core/facet_schema.h>
#include <core/fct.h>

using namespace Qt::StringLiterals;

namespace tc {

QString FacetSchema::categoryFor(const QString& facet) const
{
    return m_facetToCategory.value(facet);
}

bool FacetSchema::hasFacet(const QString& facet) const
{
    return m_facetToCategory.contains(facet);
}

const QStringList& FacetSchema::facets() const
{
    return m_facets;
}

const QStringList& FacetSchema::categories() const
{
    return m_categories;
}

void FacetSchema::addFacet(const QString& category, const QString& facet)
{
    if (category.isEmpty() || facet.isEmpty()) return;

    if (!m_categories.contains(category)) m_categories << category;
    if (m_facetToCategory.contains(facet)) return;

    m_facetToCategory.insert(facet, category);
    m_facets << facet;
}

std::expected<FacetSchema, LoadError> readFacetSchema(const QString& path)
{
    const std::expected<FctDoc, LoadError> doc = readFct(path);
    if (!doc) return std::unexpected(doc.error());

    FacetSchema schema;
    for (const FctBlock& b : doc->blocks) {
        if (b.kind != "category"_L1 || b.name.isEmpty()) continue;
        for (const FctLine& l : b.lines) {
            if (l.trivia || !l.key.isEmpty()) continue;
            for (const QString& facet : l.values)
                schema.addFacet(b.name, facet);
        }
    }
    return schema;
}

std::expected<void, LoadError> appendCategory(FacetSchema& schema, const QString& path,
                                              const QString& category, const QStringList& facets)
{
    if (category.isEmpty()) return {};

    QStringList fresh;
    for (const QString& f : facets) {
        const QString t = f.trimmed();
        if (t.isEmpty() || schema.hasFacet(t) || fresh.contains(t)) continue;
        fresh << t;
    }
    if (fresh.isEmpty()) return {};

    std::expected<FctDoc, LoadError> doc = readFct(path);
    if (!doc) return std::unexpected(doc.error());

    if (!doc->blocks.isEmpty()) doc->blocks.last().lines << FctLine{{}, {}, {}, true};

    FctBlock block;
    block.kind = u"category"_s;
    block.name = category;
    block.lines << FctLine{{}, fresh, {}, false};
    doc->blocks << block;
    doc->trailingNewline = true;

    const std::expected<void, LoadError> w = writeFct(*doc, path);
    if (!w) return w;

    for (const QString& f : fresh)
        schema.addFacet(category, f);

    return {};
}

} // namespace tc
