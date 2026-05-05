#pragma once
#include <core/entry.h>
#include <QHash>


namespace core {

class TagIndex {
public:
    int32_t getOrCreate(const QString& tag);
    QString getTag(const int32_t) const;
    void buildIndex();

    // All tag strings currently in the index, in sorted order
    const QList<QString>& allTags() const;

    // True iff at least one entry currently references this tag.
    bool tagInUse(const QString& tag) const;

    void add(int32_t tagId, int32_t entryId);
    void remove(int32_t tagId, int32_t entryId);

    QList<int32_t> multiPrefixSearch(const QString& query) const;
    QList<int32_t> entriesForTerm(const QString& raw) const;

private:
    bool m_bulkLoading{true};
    int32_t m_nextId{0};

    QHash<int32_t, QList<int32_t>> m_tagIdToEntryId;
    QHash<int32_t, QString> m_idToTag;
    QHash<QString, int32_t> m_tagToId;

    QList<QString> m_sortedTags;
    void insertSorted(const QString& tag);
    void removeSorted(const QString& tag);

    QList<int32_t> entriesForPrefix(const QString& prefix) const;
    template <typename T> static QList<T> intersectSorted(const QList<T>& a, const QList<T>& b);
};

} // namespace core