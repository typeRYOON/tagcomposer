#include <core/facetindex.h>
#include <QFile>
#include <QTextStream>
#include <algorithm>

namespace core {

// ---- Helpers

static QList<QString> splitTrimmed(const QString& s, QChar sep)
{
    QList<QString> out;
    for (const QString& p : s.split(sep))
        if (const QString t = p.trimmed(); !t.isEmpty()) out << t;
    return out;
}

// ---- Load
// schema format:
//   @category Name   - open a new category
//   f, f, f          - facets in the current category
//   tag = ...        - ignored here (belongs to definitionsPath)

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

    for (const QString& raw : QString::fromUtf8(f.readAll()).split('\n')) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;

        if (line.startsWith("@category")) {
            currentCat = line.mid(9).trimmed();
            if (!m_categories.contains(currentCat)) m_categories << currentCat;
            continue;
        }

        if (line.contains('=')) continue; // belongs to definitionsPath

        if (!currentCat.isEmpty()) {
            for (const QString& facet : splitTrimmed(line, ',')) {
                if (!m_facetToCategory.contains(facet)) m_facetList << facet;
                m_facetToCategory[facet] = currentCat;
            }
        }
    }
}

void FacetIndex::loadDefinitionsFromFile(const QString& definitionsPath)
{
    QFile f(definitionsPath);
    if (!f.open(QIODevice::ReadOnly)) return;

    for (const QString& raw : QString::fromUtf8(f.readAll()).split('\n')) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#') || !line.contains('=')) continue;

        // saveDefinitions writes `<tag>=<facets>` and facet names never contain
        // '=', so the last '=' is always the separator - splitting there keeps
        // tags that contain one (`:>=`, `= =`, `1=2`, `qi==qi`). Rejecting them
        // instead dropped the definition on load, and the next save then erased
        // it from disk. indexOf would truncate `foo=` to `foo` and load the
        // rest as garbage facets.
        const int eq = line.lastIndexOf('=');
        const QString tag = line.left(eq).trimmed();
        if (tag.isEmpty()) continue;
        m_tagToFacets[tag] = splitTrimmed(line.mid(eq + 1), ',');
    }
}

void FacetIndex::saveDefinitions(const QString& definitionsPath) const
{
    QFile f(definitionsPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;
    QTextStream ts(&f);
    // Sort for stable diffs - QHash iteration order is unspecified.
    QStringList tags = m_tagToFacets.keys();
    std::sort(tags.begin(), tags.end());
    for (const QString& tag : tags)
        ts << tag << '=' << m_tagToFacets[tag].join(',') << "\n";
}

// ---- Lookups

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

void FacetIndex::appendFacets(const QString& schemaPath, const QString& category,
                              const QStringList& facets)
{
    if (category.isEmpty()) return;

    QStringList toAdd;
    for (const QString& f : facets) {
        const QString trimmed = f.trimmed();
        if (trimmed.isEmpty()) continue;
        if (m_facetToCategory.contains(trimmed)) continue;
        if (toAdd.contains(trimmed)) continue;
        toAdd << trimmed;
    }
    if (toAdd.isEmpty()) return;

    QFile f(schemaPath);
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    QTextStream ts(&f);
    ts << "\n@category " << category << "\n";
    ts << toAdd.join(", ") << "\n";
    f.close();

    if (!m_categories.contains(category)) m_categories << category;
    for (const QString& fc : toAdd) {
        m_facetList << fc;
        m_facetToCategory[fc] = category;
    }
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
        if (!m_tagToFacets.contains(tag)) out << tag;
    return out;
}

} // namespace core
