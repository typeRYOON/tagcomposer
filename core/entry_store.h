#pragma once
#include <core/entry.h>
#include <core/load_error.h>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace tc {

// Owns the entry library and is the only thing that mutates it.
// Callers keep a uuid. find() returns a pointer valid until the next mutation,
// meant to be used and dropped in the same expression.
class EntryStore : public QObject {
    Q_OBJECT

public:
    explicit EntryStore(QObject* parent = nullptr);

    // Reads every entry folder under entryDir and remembers it as the root for later writes.
    QList<LoadError> load(const QString& entryDir);

    const QList<Entry>& all() const;
    const Entry* find(const QString& uuid) const;
    qsizetype count() const;

    // Assigns a uuid when the entry has none.
    // Returns the uuid, or empty when the entry is not storable or the write failed.
    QString add(Entry entry);

    // Removes the entry and its folder from disk.
    bool remove(const QString& uuid);

    bool addTag(const QString& uuid, qsizetype imageIndex, const QString& tag);
    bool removeTag(const QString& uuid, qsizetype imageIndex, const QString& tag);
    bool removeImage(const QString& uuid, qsizetype imageIndex);

    bool setTitle(const QString& uuid, const QString& title);
    bool setComment(const QString& uuid, const QString& comment);
    bool setLora(const QString& uuid, const std::optional<Lora>& lora);

    // Absolute path of an entry's folder, for callers that need its images.
    QString folderFor(const QString& uuid) const;

signals:
    // The whole library was replaced. Anything derived from it is stale.
    void reloaded();

    void entryAdded(const QString& uuid);
    void entryChanged(const QString& uuid);

    // The entry is already gone when this fires; drop anything keyed on it.
    void entryRemoved(const QString& uuid);

    // Holders of (uuid, imageIndex) must drop the removed slot
    // and shift higher indices down by one.
    void imageRemoved(const QString& uuid, qsizetype imageIndex);

    void writeFailed(const QString& uuid, const QString& reason);

private:
    Entry* mutableEntry(const QString& uuid);
    bool persist(const Entry& entry);
    void reindex();

    QString m_root;
    QList<Entry> m_entries;
    QHash<QString, qsizetype> m_indexByUuid;
};

} // namespace tc
