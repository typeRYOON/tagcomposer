#include <core/danbooru_index.h>
#include <core/entry.h>
#include <QFile>
#include <QSet>
#include <QTextStream>
#include <algorithm>
#include <numeric>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

QStringList parseAliases(QString field)
{
    field = field.trimmed();
    if (field.isEmpty()) return {};
    if (field.startsWith(u'"') && field.endsWith(u'"'))
        field = field.sliced(1, field.size() - 2);
    return field.split(u',', Qt::SkipEmptyParts);
}

} // namespace

bool DanbooruIndex::load(const QString& path)
{
    m_tags.clear();
    m_byName.clear();
    m_byAlias.clear();
    m_byWord.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

    QTextStream in(&file);
    in.readLine(); // header: tag,category,count,wrong

    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.isEmpty()) continue;

        const qsizetype first = line.indexOf(u',');
        if (first < 0) continue;
        const qsizetype second = line.indexOf(u',', first + 1);
        if (second < 0) continue;
        const qsizetype third = line.indexOf(u',', second + 1);

        Tag tag;
        tag.name = line.first(first);
        tag.category = line.sliced(first + 1, second - first - 1).toInt();
        tag.count = third < 0 ? line.sliced(second + 1).toLongLong()
                              : line.sliced(second + 1, third - second - 1).toLongLong();
        if (third >= 0) tag.aliases = parseAliases(line.sliced(third + 1));

        m_tags << std::move(tag);
    }

    m_byName.resize(m_tags.size());
    std::iota(m_byName.begin(), m_byName.end(), qsizetype(0));
    std::sort(m_byName.begin(), m_byName.end(),
              [this](qsizetype a, qsizetype b) { return m_tags[a].name < m_tags[b].name; });

    for (qsizetype i = 0; i < m_tags.size(); ++i) {
        for (const QString& alias : m_tags[i].aliases) {
            m_byAlias << AliasEntry{alias.startsWith(u'/') ? alias.sliced(1) : alias, alias, i};
        }
    }
    std::sort(m_byAlias.begin(), m_byAlias.end(),
              [](const AliasEntry& a, const AliasEntry& b) { return a.normalized < b.normalized; });

    // Only the words after the first: the leading one is already reachable
    // through m_byName.
    for (qsizetype i = 0; i < m_tags.size(); ++i) {
        const QString& name = m_tags[i].name;
        qsizetype wordStart = 0;
        bool leading = true;

        for (qsizetype k = 0; k <= name.size(); ++k) {
            if (k != name.size() && name[k] != u' ') continue;
            if (!leading && k > wordStart)
                m_byWord << WordEntry{name.sliced(wordStart, k - wordStart), i, int(wordStart)};
            leading = false;
            wordStart = k + 1;
        }
    }
    std::sort(m_byWord.begin(), m_byWord.end(),
              [](const WordEntry& a, const WordEntry& b) { return a.word < b.word; });

    return true;
}

bool DanbooruIndex::isEmpty() const
{
    return m_tags.isEmpty();
}

QList<TagSearchResult> DanbooruIndex::search(const QString& prefix, int maxResults) const
{
    if (prefix.isEmpty() || m_tags.isEmpty()) return {};

    const QString needle = normalizeTag(prefix);
    if (needle.isEmpty()) return {};

    const int length = int(needle.size());
    QSet<qsizetype> seen;
    QList<TagSearchResult> byName;
    QList<TagSearchResult> byWord;
    QList<TagSearchResult> byAlias;

    {
        auto it = std::lower_bound(m_byName.cbegin(), m_byName.cend(), needle,
                                   [this](qsizetype index, const QString& key) {
                                       return m_tags[index].name < key;
                                   });
        for (; it != m_byName.cend(); ++it) {
            const Tag& tag = m_tags[*it];
            if (!tag.name.startsWith(needle)) break;
            seen.insert(*it);
            byName << TagSearchResult{tag.name,  tag.name, tag.category,
                                      tag.count, false,    0,
                                      length};
        }
    }

    {
        auto it = std::lower_bound(
            m_byWord.cbegin(), m_byWord.cend(), needle,
            [](const WordEntry& entry, const QString& key) { return entry.word < key; });
        for (; it != m_byWord.cend(); ++it) {
            if (!it->word.startsWith(needle)) break;
            if (seen.contains(it->tagIndex)) continue;
            seen.insert(it->tagIndex);

            const Tag& tag = m_tags[it->tagIndex];
            byWord << TagSearchResult{tag.name,  tag.name, tag.category,
                                      tag.count, false,    it->offset,
                                      length};
        }
    }

    // Aliases get their own rows: the pill shows where the alias resolves to.
    {
        auto it = std::lower_bound(
            m_byAlias.cbegin(), m_byAlias.cend(), needle,
            [](const AliasEntry& entry, const QString& key) { return entry.normalized < key; });
        for (; it != m_byAlias.cend(); ++it) {
            if (!it->normalized.startsWith(needle)) break;

            const Tag& tag = m_tags[it->tagIndex];
            byAlias << TagSearchResult{it->original, tag.name, tag.category,
                                       tag.count,    true,     it->original.startsWith(u'/') ? 1 : 0,
                                       length};
        }
    }

    const auto byCount = [](const TagSearchResult& a, const TagSearchResult& b) {
        return a.count > b.count;
    };
    std::sort(byName.begin(), byName.end(), byCount);
    std::sort(byWord.begin(), byWord.end(), byCount);
    std::sort(byAlias.begin(), byAlias.end(), byCount);

    QList<TagSearchResult> results;
    results.reserve(maxResults);
    for (QList<TagSearchResult>* tier : {&byName, &byWord, &byAlias}) {
        for (TagSearchResult& result : *tier) {
            if (results.size() >= maxResults) return results;
            results << std::move(result);
        }
    }
    return results;
}

qsizetype DanbooruIndex::indexOfName(const QString& tag) const
{
    auto it = std::lower_bound(
        m_byName.cbegin(), m_byName.cend(), tag,
        [this](qsizetype index, const QString& key) { return m_tags[index].name < key; });
    if (it == m_byName.cend() || m_tags[*it].name != tag) return -1;
    return *it;
}

int DanbooruIndex::tagCategory(const QString& tag) const
{
    const qsizetype index = indexOfName(tag);
    return index < 0 ? -1 : m_tags[index].category;
}

qint64 DanbooruIndex::tagCount(const QString& tag) const
{
    const qsizetype index = indexOfName(tag);
    return index < 0 ? -1 : m_tags[index].count;
}

} // namespace tc
