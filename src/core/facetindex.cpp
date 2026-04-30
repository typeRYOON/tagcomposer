#include <core/facetindex.h>
#include <QFile>
#include <QTextStream>

namespace core {

// ── Helpers ───────────────────────────────────────────────────────────────────

static QList<QString> splitTrimmed(const QString& s, QChar sep)
{
    QList<QString> out;
    for (const QString& p : s.split(sep))
        if (const QString t = p.trimmed(); !t.isEmpty())
            out << t;
    return out;
}

// ── Load ──────────────────────────────────────────────────────────────────────

//  schemaPath format:
//    @category Name   → open a new category
//    f, f, f, ...     → facet declarations for current category
//    tag = ...        → ignored (belongs in definitionsPath)

FacetIndex FacetIndex::loadFromFile(const QString& schemaPath)
{
    FacetIndex idx;
    idx.reloadSchemaFromFile(schemaPath);
    return idx;
}

void FacetIndex::reloadSchemaFromFile(const QString& schemaPath)
{
    m_categories.clear();
    m_facetList.clear();
    m_facetToCategory.clear();

    QFile f(schemaPath);
    if (!f.open(QIODevice::ReadOnly)) return;

    QString currentCat;

    for (const QString& raw : QString::fromUtf8(f.readAll()).split('\n'))
    {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;

        if (line.startsWith("@category")) {
            currentCat = line.mid(9).trimmed();
            if (!m_categories.contains(currentCat))
                m_categories << currentCat;
            continue;
        }

        if (line.contains('=')) continue; // tag definitions live in definitionsPath

        if (!currentCat.isEmpty()) {
            for (const QString& facet : splitTrimmed(line, ',')) {
                if (!m_facetToCategory.contains(facet))
                    m_facetList << facet;
                m_facetToCategory[facet] = currentCat;
            }
        }
    }
}

void FacetIndex::loadDefinitionsFromFile(const QString& definitionsPath)
{
    QFile f(definitionsPath);
    if (!f.open(QIODevice::ReadOnly)) return;

    for (const QString& raw : QString::fromUtf8(f.readAll()).split('\n'))
    {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#') || !line.contains('=')) continue;

        const int eq                 = line.indexOf('=');
        const QString tag            = line.left(eq).trimmed();
        const QList<QString> facets  = splitTrimmed(line.mid(eq + 1), ',');
        if (!tag.isEmpty())
            m_tagToFacets[tag] = facets;
    }
}

void FacetIndex::saveDefinitions(const QString& definitionsPath) const
{
    QFile f(definitionsPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;
    QTextStream ts(&f);
    for (auto it = m_tagToFacets.constBegin(); it != m_tagToFacets.constEnd(); ++it)
        ts << it.key() << '=' << it.value().join(',') << "\n";
}

// ── Lookups ───────────────────────────────────────────────────────────────────

QList<QString> FacetIndex::facetsFor(const QString& tag) const
{
    return m_tagToFacets.value(tag);
}

bool FacetIndex::hasFacets(const QString& tag) const
{
    return m_tagToFacets.contains(tag);
}

void FacetIndex::setDefinition(const QString& tag, const QList<QString>& facets)
{
    if (facets.isEmpty())
        m_tagToFacets.remove(tag);
    else
        m_tagToFacets[tag] = facets;
}

QString FacetIndex::categoryFor(const QString& facet) const
{
    return m_facetToCategory.value(facet);
}

QList<QString> FacetIndex::allCategories() const
{
    return m_categories;
}

QList<QString> FacetIndex::allFacets() const
{
    return m_facetList;
}

QList<QString> FacetIndex::allDefinedTags() const
{
    return m_tagToFacets.keys();
}

QList<QString> FacetIndex::undefined(const QList<QString>& tags) const
{
    QList<QString> out;
    for (const QString& tag : tags)
        if (!m_tagToFacets.contains(tag))
            out << tag;
    return out;
}

} // namespace core
