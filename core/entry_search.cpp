#include <core/entry_search.h>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

int totalTagCount(const Entry& entry)
{
    int n = 0;
    for (const EntryImage& image : entry.images)
        n += int(image.tags.size());
    return n;
}

TagTerm makeTerm(QString text)
{
    TagTerm term;
    term.exact = text.endsWith(u']');
    if (term.exact) text.chop(1);
    term.text = text.trimmed().toLower();
    return term;
}

NumFilter parseNumFilter(QStringView text)
{
    NumFilter filter;
    const QString body = text.trimmed().toString();
    if (body.isEmpty()) return filter;

    NumFilter::Op op = NumFilter::Op::Eq;
    QStringView rest(body);

    if (rest.startsWith(">="_L1)) {
        op = NumFilter::Op::Ge;
        rest = rest.sliced(2);
    }
    else if (rest.startsWith("<="_L1)) {
        op = NumFilter::Op::Le;
        rest = rest.sliced(2);
    }
    else if (rest.startsWith(u'>')) {
        op = NumFilter::Op::Gt;
        rest = rest.sliced(1);
    }
    else if (rest.startsWith(u'<')) {
        op = NumFilter::Op::Lt;
        rest = rest.sliced(1);
    }
    else if (rest.startsWith(u'=')) {
        rest = rest.sliced(1);
    }

    bool ok = false;
    const int value = rest.trimmed().toInt(&ok);
    // A bad bound disables the clause instead of matching nothing.
    if (!ok || value < 0) return filter;

    filter.op = op;
    filter.value = value;
    return filter;
}

void applyPresence(QueryGroup& group, QStringView field, bool wanted)
{
    const QString name = field.trimmed().toString().toLower();
    if (name == "lora"_L1)
        group.hasLora = wanted;
    else if (name == "title"_L1)
        group.hasTitle = wanted;
    else if (name == "comment"_L1)
        group.hasComment = wanted;
    // Unknown fields are ignored.
}

bool matchesScalarClauses(const Entry& entry, const QueryGroup& group)
{
    if (!group.title.isEmpty() && !entry.title.contains(group.title, Qt::CaseInsensitive))
        return false;
    if (!group.comment.isEmpty() && !entry.comment.contains(group.comment, Qt::CaseInsensitive))
        return false;

    if (!group.lora.isEmpty()) {
        if (!entry.lora) return false;
        const QString needle = group.lora.toLower();
        const bool byName = QFileInfo(entry.lora->file).baseName().toLower().contains(needle);
        const bool byHash = entry.lora->sha256.startsWith(needle);
        if (!byName && !byHash) return false;
    }

    if (group.hasLora && entry.lora.has_value() != *group.hasLora) return false;
    if (group.hasTitle && !entry.title.isEmpty() != *group.hasTitle) return false;
    if (group.hasComment && !entry.comment.isEmpty() != *group.hasComment) return false;

    if (group.imageCount.active() && !group.imageCount.match(int(entry.images.size())))
        return false;
    if (group.tagCount.active() && !group.tagCount.match(totalTagCount(entry))) return false;

    return true;
}

QList<qsizetype> intersectSorted(const QList<qsizetype>& a, const QList<qsizetype>& b)
{
    QList<qsizetype> out;
    out.reserve(std::min(a.size(), b.size()));
    std::set_intersection(a.cbegin(), a.cend(), b.cbegin(), b.cend(), std::back_inserter(out));
    return out;
}

} // namespace

bool NumFilter::active() const
{
    return op != Op::Off;
}

bool NumFilter::match(int n) const
{
    switch (op) {
    case Op::Off:
        return true;
    case Op::Eq:
        return n == value;
    case Op::Lt:
        return n < value;
    case Op::Le:
        return n <= value;
    case Op::Gt:
        return n > value;
    case Op::Ge:
        return n >= value;
    }
    return true;
}

EntryQuery parseEntryQuery(const QString& text)
{
    EntryQuery query;

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        query.groups << QueryGroup{};
        return query;
    }

    for (const QString& rawGroup : trimmed.split(u'|', Qt::KeepEmptyParts)) {
        const QString groupText = rawGroup.trimmed();
        if (groupText.isEmpty()) continue;

        QueryGroup group;
        for (const QString& rawPart : groupText.split(u',', Qt::SkipEmptyParts)) {
            const QString part = rawPart.trimmed();
            if (part.isEmpty()) continue;

            const QStringView view(part);
            if (view.startsWith("title:"_L1, Qt::CaseInsensitive))
                group.title = view.sliced(6).trimmed().toString();
            else if (view.startsWith("comment:"_L1, Qt::CaseInsensitive))
                group.comment = view.sliced(8).trimmed().toString();
            else if (view.startsWith("lora:"_L1, Qt::CaseInsensitive))
                group.lora = view.sliced(5).trimmed().toString();
            else if (view.startsWith("images:"_L1, Qt::CaseInsensitive))
                group.imageCount = parseNumFilter(view.sliced(7));
            else if (view.startsWith("tags:"_L1, Qt::CaseInsensitive))
                group.tagCount = parseNumFilter(view.sliced(5));
            else if (view.startsWith("has:"_L1, Qt::CaseInsensitive))
                applyPresence(group, view.sliced(4), true);
            else if (view.startsWith("missing:"_L1, Qt::CaseInsensitive))
                applyPresence(group, view.sliced(8), false);
            else if (view.startsWith("sort:"_L1, Qt::CaseInsensitive)) {
                const QString spec = view.sliced(5).trimmed().toString().toLower();
                const qsizetype colon = spec.indexOf(u':');
                query.sortKey = (colon < 0) ? spec : spec.first(colon).trimmed();
                query.sortDir = (colon < 0) ? QString() : spec.sliced(colon + 1).trimmed();
            }
            else if (part.startsWith(u'-') && part.size() > 1)
                group.excluded << makeTerm(part.sliced(1));
            else
                group.tags << makeTerm(part);
        }
        query.groups << group;
    }

    if (query.groups.isEmpty()) query.groups << QueryGroup{};
    return query;
}

void sortEntries(QList<const Entry*>& entries, const QString& sortKey, const QString& sortDir)
{
    using Cmp = bool (*)(const Entry*, const Entry*);

    Cmp ascending = [](const Entry* a, const Entry* b) { return a->created < b->created; };
    bool ascendingByDefault = false;

    if (sortKey == "title"_L1) {
        ascending = [](const Entry* a, const Entry* b) {
            const int c = a->title.compare(b->title, Qt::CaseInsensitive);
            return c != 0 ? c < 0 : a->created < b->created;
        };
        ascendingByDefault = true;
    }
    else if (sortKey == "images"_L1) {
        ascending = [](const Entry* a, const Entry* b) {
            return a->images.size() != b->images.size() ? a->images.size() < b->images.size()
                                                        : a->created < b->created;
        };
    }
    else if (sortKey == "tags"_L1) {
        ascending = [](const Entry* a, const Entry* b) {
            const int na = totalTagCount(*a);
            const int nb = totalTagCount(*b);
            return na != nb ? na < nb : a->created < b->created;
        };
    }

    const bool asc = (sortDir == "asc"_L1)    ? true
                     : (sortDir == "desc"_L1) ? false
                                              : ascendingByDefault;

    std::stable_sort(entries.begin(), entries.end(), [ascending, asc](const Entry* a, const Entry* b) {
        return asc ? ascending(a, b) : ascending(b, a);
    });
}

EntrySearch::EntrySearch(const EntryStore& store, QObject* parent)
    : QObject(parent), m_store(&store)
{
    const auto markDirty = [this]() { m_dirty = true; };
    connect(&store, &EntryStore::reloaded, this, markDirty);
    connect(&store, &EntryStore::entryAdded, this, markDirty);
    connect(&store, &EntryStore::entryChanged, this, markDirty);
    connect(&store, &EntryStore::entryRemoved, this, markDirty);
    connect(&store, &EntryStore::imageRemoved, this, markDirty);
}

void EntrySearch::invalidate()
{
    m_dirty = true;
}

bool EntrySearch::dirty() const
{
    return m_dirty;
}

qsizetype EntrySearch::indexedTags() const
{
    if (m_dirty) rebuild();
    return m_sortedTags.size();
}

void EntrySearch::rebuild() const
{
    m_postings.clear();
    m_sortedTags.clear();

    const QList<Entry>& entries = m_store->all();
    for (qsizetype i = 0; i < entries.size(); ++i) {
        for (const EntryImage& image : entries[i].images) {
            for (const QString& tag : image.tags) {
                QList<qsizetype>& posting = m_postings[tag];
                if (posting.isEmpty() || posting.last() != i) posting << i;
            }
        }
    }

    m_sortedTags = m_postings.keys();
    m_sortedTags.sort();
    m_dirty = false;
}

QList<qsizetype> EntrySearch::forTerm(const TagTerm& term) const
{
    if (term.text.isEmpty()) return {};

    if (term.exact) return m_postings.value(term.text);

    QList<qsizetype> merged;
    auto it = std::lower_bound(m_sortedTags.cbegin(), m_sortedTags.cend(), term.text);
    for (; it != m_sortedTags.cend() && it->startsWith(term.text); ++it) {
        const QList<qsizetype>& posting = m_postings.value(*it);
        QList<qsizetype> next;
        next.reserve(merged.size() + posting.size());
        std::set_union(merged.cbegin(), merged.cend(), posting.cbegin(), posting.cend(),
                       std::back_inserter(next));
        merged = std::move(next);
    }
    return merged;
}

QList<qsizetype> EntrySearch::candidates(const QueryGroup& group) const
{
    QList<qsizetype> result;

    if (group.tags.isEmpty()) {
        result.reserve(m_store->count());
        for (qsizetype i = 0; i < m_store->count(); ++i)
            result << i;
    }
    else {
        QList<QList<qsizetype>> perTerm;
        perTerm.reserve(group.tags.size());
        for (const TagTerm& term : group.tags) {
            QList<qsizetype> ids = forTerm(term);
            if (ids.isEmpty()) return {};
            perTerm << std::move(ids);
        }
        std::sort(perTerm.begin(), perTerm.end(),
                  [](const QList<qsizetype>& a, const QList<qsizetype>& b) {
                      return a.size() < b.size();
                  });

        result = perTerm.first();
        for (qsizetype i = 1; i < perTerm.size() && !result.isEmpty(); ++i)
            result = intersectSorted(result, perTerm[i]);
    }

    if (!group.excluded.isEmpty() && !result.isEmpty()) {
        QSet<qsizetype> drop;
        for (const TagTerm& term : group.excluded)
            for (qsizetype id : forTerm(term))
                drop.insert(id);

        if (!drop.isEmpty()) {
            QList<qsizetype> kept;
            kept.reserve(result.size());
            for (qsizetype id : result)
                if (!drop.contains(id)) kept << id;
            result = std::move(kept);
        }
    }

    return result;
}

QStringList EntrySearch::find(const QString& query) const
{
    return find(parseEntryQuery(query));
}

QStringList EntrySearch::find(const EntryQuery& query) const
{
    if (m_dirty) rebuild();

    const QList<Entry>& entries = m_store->all();

    QList<const Entry*> matched;
    QSet<qsizetype> seen;

    for (const QueryGroup& group : query.groups) {
        for (qsizetype id : candidates(group)) {
            if (seen.contains(id)) continue;
            if (!matchesScalarClauses(entries[id], group)) continue;
            seen.insert(id);
            matched << &entries[id];
        }
    }

    sortEntries(matched, query.sortKey, query.sortDir);

    QStringList uuids;
    uuids.reserve(matched.size());
    for (const Entry* entry : matched)
        uuids << entry->uuid;
    return uuids;
}

} // namespace tc
