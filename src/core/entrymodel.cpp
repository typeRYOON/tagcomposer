#include <core/entrymodel.h>
#include <core/entryio.h>
#include <utils/appconfig.h>
#include <utils/stringutils.h>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

using namespace utils;

namespace core {

EntryModel::EntryModel(QObject* parent) : QObject{parent}
{
    for (auto& e : EntryIO::loadAll(m_tagIndex, BASE_PATH + "/data/entry"))
        m_entries.push_back(std::move(e));

    // std::list nodes are stable; the pointers in m_entryByIndex stay valid.
    m_entryByIndex.resize(m_entries.size());
    for (auto& e : m_entries)
        m_entryByIndex[e.id] = &e;
    buildIndex();
}


QList<Entry*> EntryModel::filter(const QString& query)
{
    QList<Entry*> ret;
    const QString query_in = query.trimmed();
    QString sortKey;
    QString sortDir;

    if (query_in.isEmpty()) {
        ret.reserve(m_entryByIndex.size());
        for (Entry* e : m_entryByIndex)
            if (e) ret << e;
    }
    else {
        // '|' is the OR separator between AND-groups. Single-group queries
        // (no '|') run filterAndGroup exactly once and behave identically to
        // the previous parser. The literal "| |" Danbooru tag is shadowed by
        // this rule -- acceptable trade-off, very narrow tag.
        const QStringList groups = query_in.split('|', Qt::KeepEmptyParts);
        QSet<int32_t> seen;
        for (const QString& g : groups) {
            const QString gt = g.trimmed();
            if (gt.isEmpty()) continue; // skip "||", trailing/leading "|"
            QString gSortKey;
            QString gSortDir;
            const QList<Entry*> sub = filterAndGroup(gt, gSortKey, gSortDir);
            // Last group's sort: wins; empty groups don't overwrite an
            // earlier directive.
            if (!gSortKey.isEmpty()) {
                sortKey = gSortKey;
                sortDir = gSortDir;
            }
            for (Entry* e : sub) {
                if (!e) continue;
                const int32_t id = int32_t(e->id);
                if (seen.contains(id)) continue;
                seen.insert(id);
                ret << e;
            }
        }
    }

    applySortSpec(ret, sortKey, sortDir);
    return ret;
}

QList<Entry*> EntryModel::filterAndGroup(const QString& sub, QString& outSortKey,
                                         QString& outSortDir)
{
    QList<Entry*> ret;
    QString titleTerm;
    QString loraTerm;
    QStringList tagParts;
    QStringList negParts;

    for (const QString& part : sub.split(',', Qt::SkipEmptyParts)) {
        const QString t = part.trimmed();
        if (t.startsWith("title:", Qt::CaseInsensitive))
            titleTerm = t.mid(6).trimmed();
        else if (t.startsWith("lora:", Qt::CaseInsensitive))
            loraTerm = t.mid(5).trimmed();
        else if (t.startsWith("sort:", Qt::CaseInsensitive)) {
            // sort:<key>[:asc|:desc] - global across the whole query; the
            // outer filter() picks the last non-empty value.
            const QString rest = t.mid(5).trimmed().toLower();
            const int colon = rest.indexOf(':');
            outSortKey = (colon < 0) ? rest : rest.left(colon).trimmed();
            outSortDir = (colon < 0) ? QString() : rest.mid(colon + 1).trimmed();
        }
        else if (t.startsWith('-') && t.length() > 1)
            negParts << t.mid(1).trimmed();
        else if (!t.isEmpty())
            tagParts << t;
    }

    if (tagParts.isEmpty()) {
        ret.reserve(m_entryByIndex.size());
        for (Entry* e : m_entryByIndex)
            if (e) ret << e;
    }
    else {
        const QList<int32_t> ids = m_tagIndex.multiPrefixSearch(tagParts.join(','));
        ret.reserve(ids.size());
        for (int32_t id : ids)
            if (Entry* e = m_entryByIndex[id]) ret << e;
    }

    if (!loraTerm.isEmpty()) {
        const QString lower = loraTerm.toLower();
        auto end = std::remove_if(ret.begin(), ret.end(), [&](const Entry* e) {
            if (!e->lora.has_value()) return true;
            return !QFileInfo(e->lora->file).baseName().toLower().contains(lower) &&
                   !e->lora->sha256.startsWith(lower);
        });
        ret.erase(end, ret.end());
    }

    if (!negParts.isEmpty()) {
        QSet<int32_t> excludeIds;
        for (const QString& neg : negParts)
            for (int32_t id : m_tagIndex.entriesForTerm(neg))
                excludeIds.insert(id);
        if (!excludeIds.isEmpty()) {
            auto end = std::remove_if(ret.begin(), ret.end(), [&](const Entry* e) {
                return excludeIds.contains(int32_t(e->id));
            });
            ret.erase(end, ret.end());
        }
    }

    if (!titleTerm.isEmpty()) {
        auto end = std::remove_if(ret.begin(), ret.end(), [&](const Entry* e) {
            return !e->title.contains(titleTerm, Qt::CaseInsensitive);
        });
        ret.erase(end, ret.end());
    }

    return ret;
}

void EntryModel::applySortSpec(QList<Entry*>& list, const QString& sortKey,
                               const QString& sortDir) const
{
    // Each lambda is the "ascending" comparator; we flip its arguments below
    // if descending is wanted. keyAscDefault picks a sensible default
    // direction so `sort:title` reads alphabetically and `sort:created`
    // reads newest-first.
    using EntryCmp = bool (*)(const Entry*, const Entry*);
    EntryCmp ascCmp = [](const Entry* a, const Entry* b) {
        return a->creationTime < b->creationTime;
    };
    bool keyAscDefault = false; // created: newest first

    if (sortKey == "title") {
        ascCmp = [](const Entry* a, const Entry* b) {
            const int c = a->title.compare(b->title, Qt::CaseInsensitive);
            if (c != 0) return c < 0;
            return a->creationTime < b->creationTime;
        };
        keyAscDefault = true;
    }
    else if (sortKey == "images") {
        ascCmp = [](const Entry* a, const Entry* b) {
            if (a->images.size() != b->images.size()) return a->images.size() < b->images.size();
            return a->creationTime < b->creationTime;
        };
        keyAscDefault = false;
    }
    else if (sortKey == "tags") {
        ascCmp = [](const Entry* a, const Entry* b) {
            auto count = [](const Entry* e) {
                int n = 0;
                for (const auto& img : e->images) n += img.tagIds.size();
                return n;
            };
            const int na = count(a), nb = count(b);
            if (na != nb) return na < nb;
            return a->creationTime < b->creationTime;
        };
        keyAscDefault = false;
    }
    // `created`/`date` and unknown keys fall through to the default ascCmp.

    const bool asc = (sortDir == "asc") ? true : (sortDir == "desc") ? false : keyAscDefault;
    std::sort(list.begin(), list.end(), [ascCmp, asc](const Entry* a, const Entry* b) {
        return asc ? ascCmp(a, b) : ascCmp(b, a);
    });
}


TagIndex& EntryModel::tagIndex()
{
    return m_tagIndex;
}


QList<QString> EntryModel::getTags(const QList<int32_t>& tagIds) const
{
    QList<QString> ret;
    ret.reserve(tagIds.size());
    for (const int32_t& tagId : tagIds) {
        ret << m_tagIndex.getTag(tagId);
    }

    return ret;
}


QList<int32_t> EntryModel::getTagIds(const QList<QString>& tags)
{
    QList<int32_t> ret;
    ret.reserve(tags.size());
    for (const QString& tag : tags) {
        ret << m_tagIndex.getOrCreate(tag);
    }

    return ret;
}

void EntryModel::addEntry(Entry entry)
{
    entry.id = m_entryByIndex.size();
    entry.modified = true;

    m_entries.push_back(std::move(entry));
    Entry* e = &m_entries.back();

    m_entryByIndex << e;

    const auto tags = collectTags(*e);
    for (int32_t tagId : tags)
        m_tagIndex.add(tagId, e->id);

    EntryIO::save(*e, m_tagIndex);
}


void EntryModel::deleteEntry(int32_t entryId)
{
    if (entryId < 0 || entryId >= m_entryByIndex.size()) return;
    Entry* e = m_entryByIndex[entryId];
    if (!e) return;

    const QString uuid = e->uuid; // capture before erase invalidates e

    const auto tags = collectTags(*e);
    for (int32_t tagId : tags)
        m_tagIndex.remove(tagId, entryId);

    QDir(BASE_PATH + "/data/entry/" + uuid).removeRecursively();

    // Hard-delete the list node; addEntry only appends so the nulled slot
    // is never reclaimed and stale ids stay safely null.
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (&*it == e) {
            m_entries.erase(it);
            break;
        }
    }
    m_entryByIndex[entryId] = nullptr;

    emit entryDeleted(entryId, uuid);
}


void EntryModel::updateEntry(int32_t entryId, const Entry& updated)
{
    Entry* e = m_entryByIndex[entryId];
    if (!e) return;

    const auto oldTags = collectTags(*e);
    const auto newTags = collectTags(updated);

    for (int32_t tagId : oldTags)
        if (!newTags.contains(tagId)) m_tagIndex.remove(tagId, entryId);

    for (int32_t tagId : newTags)
        if (!oldTags.contains(tagId)) m_tagIndex.add(tagId, entryId);

    *e = updated;
    e->id = entryId;
    e->modified = true;
    EntryIO::save(*e, m_tagIndex);
}

Entry* EntryModel::entryById(const int32_t id)
{
    if (id < 0 || id >= m_entryByIndex.size()) return nullptr;
    return m_entryByIndex[id];
}

Entry* EntryModel::entryByUuid(const QString& uuid)
{
    for (Entry* e : m_entryByIndex)
        if (e && e->uuid == uuid) return e;
    return nullptr;
}

Entry* EntryModel::entryByLoraSha256(const QString& sha256)
{
    if (sha256.isEmpty()) return nullptr;
    for (Entry* e : m_entryByIndex)
        if (e && e->lora.has_value() && e->lora->sha256 == sha256) return e;
    return nullptr;
}

void EntryModel::addTagToImage(int32_t entryId, int imageIdx, const QString& tag)
{
    if (entryId < 0 || entryId >= m_entryByIndex.size()) return;
    Entry* e = m_entryByIndex[entryId];
    if (!e || imageIdx >= e->images.size()) return;

    const int32_t tagId = m_tagIndex.getOrCreate(tag);
    if (!e->images[imageIdx].tagIds.contains(tagId)) {
        e->images[imageIdx].tagIds << tagId;
        m_tagIndex.add(tagId, entryId);
    }
    e->modified = true;
    EntryIO::save(*e, m_tagIndex);
}

void EntryModel::removeTagFromImage(int32_t entryId, int imageIdx, const QString& tag)
{
    if (entryId < 0 || entryId >= m_entryByIndex.size()) return;
    Entry* e = m_entryByIndex[entryId];
    if (!e || imageIdx >= e->images.size()) return;

    int32_t tagId = -1;
    for (const int32_t id : e->images[imageIdx].tagIds) {
        if (m_tagIndex.getTag(id) == tag) {
            tagId = id;
            break;
        }
    }
    if (tagId < 0) return;

    e->images[imageIdx].tagIds.removeOne(tagId);

    bool stillUsed = false;
    for (const auto& img : e->images) {
        if (img.tagIds.contains(tagId)) {
            stillUsed = true;
            break;
        }
    }
    if (!stillUsed) m_tagIndex.remove(tagId, entryId);

    e->modified = true;
    EntryIO::save(*e, m_tagIndex);
}


void EntryModel::removeImageFromEntry(int32_t entryId, int imageIdx)
{
    if (entryId < 0 || entryId >= m_entryByIndex.size()) return;
    Entry* e = m_entryByIndex[entryId];
    if (!e || imageIdx < 0 || imageIdx >= e->images.size()) return;

    const QString filePath =
        BASE_PATH + "/data/entry/" + e->uuid + "/" + e->images[imageIdx].fileName;

    for (int32_t tagId : e->images[imageIdx].tagIds) {
        bool usedElsewhere = false;
        for (int i = 0; i < e->images.size(); ++i) {
            if (i != imageIdx && e->images[i].tagIds.contains(tagId)) {
                usedElsewhere = true;
                break;
            }
        }
        if (!usedElsewhere) m_tagIndex.remove(tagId, entryId);
    }

    e->images.removeAt(imageIdx);
    QFile::remove(filePath);
    e->modified = true;
    EntryIO::save(*e, m_tagIndex);
    emit imageRemovedFromEntry(entryId, imageIdx);
}

void EntryModel::saveEntry(int32_t entryId)
{
    if (entryId < 0 || entryId >= m_entryByIndex.size()) return;
    Entry* e = m_entryByIndex[entryId];
    if (!e) return;
    e->modified = true;
    EntryIO::save(*e, m_tagIndex);
}

void EntryModel::buildIndex()
{
    for (const Entry* e : m_entryByIndex) {
        QSet<int32_t> uniqueTags;
        for (const auto& img : e->images) {
            for (int32_t tagId : img.tagIds) {
                uniqueTags.insert(tagId);
            }
        }

        for (int32_t tagId : uniqueTags) {
            m_tagIndex.add(tagId, e->id);
        }
    }
    m_tagIndex.buildIndex();
}


QSet<int32_t> EntryModel::collectTags(const Entry& entry) const
{
    QSet<int32_t> tags;
    for (const ImageData& img : entry.images) {
        for (int32_t tagId : img.tagIds) {
            tags.insert(tagId);
        }
    }

    return tags;
}
} // namespace core
