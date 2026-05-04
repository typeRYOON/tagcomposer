#include <core/tagindex.h>
#include <utils/stringutils.h>
#include <QDebug>
#include <algorithm>

using namespace utils;

namespace core {

    int32_t TagIndex::getOrCreate(const QString& tag)
    {
        const QString tag_norm{ normalizeTagInput(tag) };
        const auto it{ m_tagToId.find(tag_norm) };

        if (it != m_tagToId.end()) {
            return it.value();
        }
        const int32_t id = m_nextId++;
        m_tagToId[tag_norm] = id;
        m_idToTag[id]       = tag_norm;

        if (!m_bulkLoading) {
            insertSorted(tag_norm);
        }

        return id;
    }


    QString TagIndex::getTag(const int32_t id) const
    {
        return m_idToTag.value(id);
    }

    const QList<QString>& TagIndex::allTags() const
    {
        return m_sortedTags;
    }


    void TagIndex::buildIndex()
    {
        m_sortedTags = m_tagToId.keys().toVector();
        std::sort(m_sortedTags.begin(), m_sortedTags.end());
        m_bulkLoading = false;
    }


    void TagIndex::add(int32_t tagId, int32_t entryId)
    {
        QList<int32_t>& list = m_tagIdToEntryId[tagId];
        auto it = std::lower_bound(list.begin(), list.end(), entryId);
        if (it == list.end() || *it != entryId) {
            list.insert(it, entryId);
        }
    }


    void TagIndex::remove(int32_t tagId, int32_t entryId)
    {
        auto itHash = m_tagIdToEntryId.find(tagId);
        if (itHash == m_tagIdToEntryId.end()) {
            return;
        }
        auto& list = itHash.value();

        auto it = std::lower_bound(list.begin(), list.end(), entryId);
        if (it != list.end() && *it == entryId) {
            list.erase(it);
        }

        // Optional cleanup:
        if (list.isEmpty()) {
            m_tagIdToEntryId.erase(itHash);
            // DO NOT remove from m_tagToId unless you want aggressive pruning
        }
    }


    void TagIndex::insertSorted(const QString& tag)
    {
        auto it = std::lower_bound(m_sortedTags.begin(), m_sortedTags.end(), tag);
        m_sortedTags.insert(it, tag);
    }


    void TagIndex::removeSorted(const QString& tag)
    {
        auto it = std::lower_bound(m_sortedTags.begin(), m_sortedTags.end(), tag);
        if (it != m_sortedTags.end() && *it == tag) {
            m_sortedTags.erase(it);
        }
    }


    QList<int32_t> TagIndex::entriesForPrefix(const QString& prefix) const
    {
        QList<int32_t> result;
        if (prefix.isEmpty()) {
            return result;
        }

        auto it = std::lower_bound(m_sortedTags.begin(), m_sortedTags.end(), prefix);

        for (; it != m_sortedTags.end(); ++it)
        {
            if (!it->startsWith(prefix))
                break;

            auto x = m_tagToId[*it];
            const auto& ids = m_tagIdToEntryId.value(x);

            QList<int32_t> merged;
            merged.reserve(result.size() + ids.size());

            std::merge(
                result.begin(), result.end(),
                ids.begin(), ids.end(),
                std::back_inserter(merged)
            );
            merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
            result = std::move(merged);
        }

        return result;
    }


    QList<int32_t> TagIndex::entriesForTerm(const QString& raw) const
    {
        const bool exact    = raw.endsWith(']');
        const QString token = normalizeTagInput(exact ? raw.chopped(1) : raw);
        if (exact) {
            auto it = m_tagToId.find(token);
            if (it == m_tagToId.end()) return {};
            return m_tagIdToEntryId.value(it.value());
        }
        return entriesForPrefix(token);
    }


    QList<int32_t> TagIndex::multiPrefixSearch(const QString& query) const
    {
        QList<int32_t> result;
        const auto parts = query.split(',', Qt::SkipEmptyParts);
        if (parts.isEmpty()) {
            return {};
        }

        QList<QList<int32_t>> segmentResults;
        segmentResults.reserve(parts.size());

        for (const auto& part : parts)
        {
            const QString raw   = part.trimmed();
            const bool exact    = raw.endsWith(']');
            const QString token = normalizeTagInput(exact ? raw.chopped(1) : raw);

            QList<int32_t> ids;
            if (exact) {
                auto it = m_tagToId.find(token);
                if (it == m_tagToId.end()) return {};
                ids = m_tagIdToEntryId.value(it.value());
                if (ids.isEmpty()) return {};
            } else {
                ids = entriesForPrefix(token);
                if (ids.isEmpty()) return {};
            }
            segmentResults.push_back(std::move(ids));
        }

        std::sort(
            segmentResults.begin(),
            segmentResults.end(),
            [](const auto& a, const auto& b) {
                return a.size() < b.size();
            }
        );

        result = segmentResults[0];
        for (int i = 1; i < segmentResults.size(); ++i) {
            result = intersectSorted(result, segmentResults[i]);

            if (result.isEmpty())
                break;
        }

        return result;
    }


    template<typename T>
    QList<T> TagIndex::intersectSorted(
        const QList<T>& a,
        const QList<T>& b)
    {
        QList<int32_t> result;
        result.reserve(std::min(a.size(), b.size()));

        auto itA = a.begin();
        auto itB = b.begin();

        while (itA != a.end() && itB != b.end())
        {
            if (*itA < *itB) {
                ++itA;
            }
            else if (*itB < *itA) {
                ++itB;
            }
            else {
                result.push_back(*itA);
                ++itA;
                ++itB;
            }
        }

        return result;
    }

}



