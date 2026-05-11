#pragma once
#include <core/entry.h>
#include <core/tagindex.h>
#include <QObject>
#include <list>

namespace core {
class EntryModel : public QObject {
    Q_OBJECT
public:
    explicit EntryModel(QObject* parent = nullptr);
    TagIndex& tagIndex();

public slots:
    QList<Entry*> filter(const QString& query);
    QList<QString> getTags(const QList<int32_t>& tagIds) const;
    QList<int32_t> getTagIds(const QList<QString>& tags);
    void addEntry(Entry entry);
    void deleteEntry(int32_t entryId);
    void updateEntry(int32_t entryId, const Entry& updated);
    Entry* entryById(const int32_t);
    Entry* entryByUuid(const QString& uuid);
    Entry* entryByLoraSha256(const QString& sha256);
    void addTagToImage(int32_t entryId, int imageIdx, const QString& tag);
    void removeTagFromImage(int32_t entryId, int imageIdx, const QString& tag);
    void removeImageFromEntry(int32_t entryId, int imageIdx);
    void saveEntry(int32_t entryId);

signals:
    // Fired after deleteEntry; subscribers should drop cached refs to entryId.
    void entryDeleted(int32_t entryId, const QString& uuid);
    // Fired after removeImageFromEntry. Subscribers holding (entryId, imageIdx)
    // refs must drop the removed slot and shift higher indices down by one.
    void imageRemovedFromEntry(int32_t entryId, int imageIdx);

private:
    // ids are forever-monotonic: addEntry appends, deleteEntry nulls the slot
    // but never reclaims it, so m_entryByIndex[id] is either a stable pointer
    // or nullptr.
    std::list<Entry> m_entries;
    QList<Entry*> m_entryByIndex;

    TagIndex m_tagIndex;
    void buildIndex();

    QSet<int32_t> collectTags(const Entry& entry) const;

    // Filter a single AND-group (one slice between `|` separators in the
    // user's query). Returns unsorted candidates. `outSortKey/outSortDir`
    // capture any sort: directive the group contained so the outer filter
    // can apply the last group's sort to the OR-union.
    QList<Entry*> filterAndGroup(const QString& sub, QString& outSortKey,
                                 QString& outSortDir);

    // Apply a sort directive in-place. Empty key falls through to the
    // default ordering (creation time, newest first).
    void applySortSpec(QList<Entry*>& list, const QString& sortKey,
                       const QString& sortDir) const;
};
} // namespace core
