#pragma once
#include <core/entry.h>
#include <core/tagindex.h>
#include <QObject>
#include <list>

namespace model {
    class EntryModel : public QObject {
        Q_OBJECT
    public:
        explicit EntryModel(QObject* parent = nullptr);
        core::TagIndex& tagIndex();

    public slots:
        QList<core::Entry*> filter(const QString& query);
        QList<QString> getTags(const QList<int32_t>& tagIds) const;
        QList<int32_t> getTagIds(const QList<QString>& tags);
        void addEntry(core::Entry entry);
        void deleteEntry(int32_t entryId);
        void updateEntry(int32_t entryId, const core::Entry& updated);
        core::Entry* entryById(const int32_t);
        core::Entry* entryByUuid(const QString& uuid);
        core::Entry* entryByLoraSha256(const QString& sha256);
        void addTagToImage(int32_t entryId, int imageIdx, const QString& tag);
        void removeTagFromImage(int32_t entryId, int imageIdx, const QString& tag);
        void removeImageFromEntry(int32_t entryId, int imageIdx);
        void saveEntry(int32_t entryId);

    private:
        std::list<core::Entry> m_entries;
        QList<core::Entry*> m_entryByIndex;

        core::TagIndex m_tagIndex;
        void buildIndex();

        QSet<int32_t> collectTags(const core::Entry& entry) const;

    };
}
