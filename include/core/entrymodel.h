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
        // Fired after deleteEntry has removed the entry from memory and disk.
        // Subscribers should drop any cached references (composer pushes,
        // tile-view LoRA activation, etc.) to entryId / uuid.
        void entryDeleted(int32_t entryId, const QString& uuid);

    private:
        // m_entryByIndex[id] holds a pointer into m_entries (stable thanks to
        // std::list) or nullptr for ids that have been deleted. ids are
        // forever-monotonic - addEntry uses m_entryByIndex.size() as the next
        // id, never reusing slots emptied by deleteEntry.
        std::list<Entry> m_entries;
        QList<Entry*> m_entryByIndex;

        TagIndex m_tagIndex;
        void buildIndex();

        QSet<int32_t> collectTags(const Entry& entry) const;

    };
}
