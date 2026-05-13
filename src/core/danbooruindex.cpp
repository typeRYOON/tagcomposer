#include <core/danbooruindex.h>
#include <utils/stringutils.h>
#include <QFile>
#include <QTextStream>
#include <algorithm>
#include <numeric>

namespace core {

static QStringList parseAliases(const QString& field)
{
    if (field.isEmpty()) return {};
    QString f = field.trimmed();
    if (f.startsWith('"') && f.endsWith('"')) f = f.mid(1, f.size() - 2);
    return f.split(',', Qt::SkipEmptyParts);
}

DanbooruIndex* DanbooruIndex::loadFromFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return nullptr;

    auto* idx = new DanbooruIndex;
    QTextStream in(&file);
    in.readLine(); // skip header: tag,category,count,wrong

    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.isEmpty()) continue;

        const int c1 = line.indexOf(',');
        if (c1 < 0) continue;
        const int c2 = line.indexOf(',', c1 + 1);
        if (c2 < 0) continue;
        const int c3 = line.indexOf(',', c2 + 1);

        Tag t;
        t.name = line.left(c1);
        t.category = line.mid(c1 + 1, c2 - c1 - 1).toInt();
        t.count =
            (c3 < 0) ? line.mid(c2 + 1).toLongLong() : line.mid(c2 + 1, c3 - c2 - 1).toLongLong();
        if (c3 >= 0) t.aliases = parseAliases(line.mid(c3 + 1));

        idx->m_tags << std::move(t);
    }

    idx->m_byName.resize(idx->m_tags.size());
    std::iota(idx->m_byName.begin(), idx->m_byName.end(), 0);
    std::sort(idx->m_byName.begin(), idx->m_byName.end(),
              [&](int a, int b) { return idx->m_tags[a].name < idx->m_tags[b].name; });

    for (int i = 0; i < idx->m_tags.size(); ++i) {
        for (const QString& alias : idx->m_tags[i].aliases) {
            AliasEntry ae;
            ae.original = alias;
            ae.normalized = alias.startsWith('/') ? alias.mid(1) : alias;
            ae.tagIdx = i;
            idx->m_byAlias << ae;
        }
    }
    std::sort(idx->m_byAlias.begin(), idx->m_byAlias.end(),
              [](const AliasEntry& a, const AliasEntry& b) { return a.normalized < b.normalized; });

    return idx;
}


QList<TagSearchResult> DanbooruIndex::search(const QString& prefix, int maxResults) const
{
    if (prefix.isEmpty() || m_tags.isEmpty()) return {};

    const QString p = utils::normalizeTagInput(prefix);
    if (p.isEmpty()) return {};

    QList<TagSearchResult> results;

    {
        auto it = std::lower_bound(
            m_byName.cbegin(), m_byName.cend(), p,
            [&](int tagIdx, const QString& key) { return m_tags[tagIdx].name < key; });
        for (; it != m_byName.cend(); ++it) {
            const Tag& t = m_tags[*it];
            if (!t.name.startsWith(p)) break;
            results.push_back({t.name, t.name, t.category, t.count, false, 0, (int)p.size()});
        }
    }

    {
        auto it = std::lower_bound(
            m_byAlias.cbegin(), m_byAlias.cend(), p,
            [](const AliasEntry& ae, const QString& key) { return ae.normalized < key; });
        for (; it != m_byAlias.cend(); ++it) {
            if (!it->normalized.startsWith(p)) break;
            const Tag& t = m_tags[it->tagIdx];
            const int offset = it->original.startsWith('/') ? 1 : 0;
            results.push_back(
                {it->original, t.name, t.category, t.count, true, offset, (int)p.size()});
        }
    }

    std::sort(results.begin(), results.end(),
              [](const TagSearchResult& a, const TagSearchResult& b) { return a.count > b.count; });

    if (results.size() > maxResults) results.resize(maxResults);
    return results;
}

int DanbooruIndex::tagCategory(const QString& tag) const
{
    auto it = std::lower_bound(m_byName.cbegin(), m_byName.cend(), tag,
                               [&](int idx, const QString& key) { return m_tags[idx].name < key; });
    if (it != m_byName.cend() && m_tags[*it].name == tag) return m_tags[*it].category;
    return -1;
}

int64_t DanbooruIndex::tagCount(const QString& tag) const
{
    auto it = std::lower_bound(m_byName.cbegin(), m_byName.cend(), tag,
                               [&](int idx, const QString& key) { return m_tags[idx].name < key; });
    if (it != m_byName.cend() && m_tags[*it].name == tag) return m_tags[*it].count;
    return -1;
}

} // namespace core
