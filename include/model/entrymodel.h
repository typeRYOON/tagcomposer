#pragma once
#include <core/entry.h>
#include <core/tagindex.h>
#include <QObject>

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

    private:
        QList<core::Entry>  m_conceptEntries;
        QList<core::Entry>  m_modelEntries;
        QList<core::Entry*> m_entryByIndex;

        core::TagIndex m_tagIndex;
        void buildIndex();

        QSet<int32_t> collectTags(const core::Entry& entry) const;

    };
}
