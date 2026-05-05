#include <core/taggroups.h>
#include <QFile>

namespace core {

static QList<QString> splitTrimmed(const QString& s, QChar sep)
{
    QList<QString> out;
    for (const QString& p : s.split(sep))
        if (const QString t = p.trimmed(); !t.isEmpty()) out << t;
    return out;
}

// File format: `@group Name` line followed by a comma-separated facet list.
TagGroupIndex TagGroupIndex::loadFromFile(const QString& path)
{
    TagGroupIndex idx;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return idx;

    TagGroup current;
    bool inGroup = false;

    for (const QString& raw : QString::fromUtf8(f.readAll()).split('\n')) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;

        if (line.startsWith("@group")) {
            if (inGroup && !current.name.isEmpty()) idx.m_groups << current;
            current = TagGroup{};
            current.name = line.mid(6).trimmed();
            inGroup = true;
            continue;
        }

        if (inGroup) current.facets = splitTrimmed(line, ',');
    }

    if (inGroup && !current.name.isEmpty()) idx.m_groups << current;

    return idx;
}

QString TagGroupIndex::groupFor(const QList<QString>& tagFacets) const
{
    for (const TagGroup& g : m_groups) {
        if (g.facets.isEmpty()) continue;
        bool allMatch = true;
        for (const QString& f : g.facets)
            if (!tagFacets.contains(f)) {
                allMatch = false;
                break;
            }
        if (allMatch) return g.name;
    }
    return {};
}

const QList<TagGroup>& TagGroupIndex::groups() const
{
    return m_groups;
}

} // namespace core
