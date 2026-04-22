#include <model/entrymodel.h>
#include <io/entryio.h>
#include <utils/appconfig.h>
#include <QDebug>

using namespace core;
using namespace utils;
using namespace io;

namespace model {

    EntryModel::EntryModel(QObject* parent) : QObject{ parent }
    {
        m_conceptEntries = EntryIO::loadAll(
            m_tagIndex,
            BASE_PATH + "/data/user/concept",
            EntryType::Concept
        );
        m_modelEntries = EntryIO::loadAll(
            m_tagIndex,
            BASE_PATH + "/data/user/model",
            EntryType::Model
        );

        // Build id(index) -> entry:
        m_entryByIndex.resize(
            m_conceptEntries.size() + m_modelEntries.size()
        );
        auto process = [&](QList<Entry>& list)
        {
            for (auto& e : list)
            {
                m_entryByIndex[e.id] = &e;
            }
        };
        process(m_conceptEntries);
        process(m_modelEntries);
        buildIndex();
    }


    QList<Entry*> EntryModel::filter(const QString& query)
    {
        QList<Entry*> ret;
        QString query_in = query.trimmed();
        // If empty query, show all
        if (query_in.isEmpty())
        {
            ret.reserve(m_entryByIndex.size());
            for (auto& e : m_entryByIndex)
            {
                if (e) {
                    ret << e;
                }
            }

            return ret;
        }

        QList<int32_t> ids = m_tagIndex.multiPrefixSearch(query_in);
        ret.reserve(ids.size());

        for (const int32_t& id : ids)
        {
            Entry* e = m_entryByIndex[id];
            if (e) {
                ret << e;
            }
        }

        return ret;
    }


    core::TagIndex& EntryModel::tagIndex()
    {
        return m_tagIndex;
    }


    QList<QString> EntryModel::getTags(const QList<int32_t>& tagIds) const
    {
        QList<QString> ret;
        ret.reserve(tagIds.size());
        for (const int32_t& tagId : tagIds)
        {
            ret << m_tagIndex.getTag(tagId);
        }

        return ret;
    }


    QList<int32_t> EntryModel::getTagIds(const QList<QString>& tags)
    {
        QList<int32_t> ret;
        ret.reserve(tags.size());
        for (const QString& tag : tags)
        {
            ret << m_tagIndex.getOrCreate(tag);
        }

        return ret;
    }

    void EntryModel::addEntry(Entry entry)
    {
        // assign new ID
        entry.id = m_entryByIndex.size();

        // store (pick correct container)
        if (entry.type == EntryType::Concept)
            m_conceptEntries << entry;
        else
            m_modelEntries << entry;

        Entry* e = &(
            entry.type == EntryType::Concept
            ? m_conceptEntries.last()
            : m_modelEntries.last()
        );
        m_entryByIndex << e;

        // index it
        const auto tags = collectTags(*e);
        for (int32_t tagId : tags) {
            m_tagIndex.add(tagId, e->id);
        }
    }


    void EntryModel::deleteEntry(int32_t entryId)
    {
        Entry* e = m_entryByIndex[entryId];
        if (!e) {
            return;
        }

        const auto tags = collectTags(*e);

        for (int32_t tagId : tags) {
            m_tagIndex.remove(tagId, entryId);
        }

        m_entryByIndex[entryId] = nullptr;
    }


    void EntryModel::updateEntry(int32_t entryId, const Entry& updated)
    {
        Entry* e = m_entryByIndex[entryId];
        if (!e) {
            return;
        }

        const auto oldTags = collectTags(*e);
        const auto newTags = collectTags(updated);

        // Remove old-only tags:
        for (int32_t tagId : oldTags)
        {
            if (!newTags.contains(tagId)) {
                m_tagIndex.remove(tagId, entryId);
            }
        }
        // Add new-only tags:
        for (int32_t tagId : newTags)
        {
            if (!oldTags.contains(tagId)) {
                m_tagIndex.add(tagId, entryId);
            }
        }

        *e    = updated;    // preserve
        e->id = entryId; // preserve index
    }

    Entry* EntryModel::entryById(const int32_t id)
    {
        return m_entryByIndex[id];
    }


    void EntryModel::buildIndex()
    {
        for (const Entry* e : m_entryByIndex)
        {
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
}
