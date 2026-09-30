#pragma once
#include <core/entry.h>
#include <core/load_error.h>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace tc {

// Owns and mutates the entry library. Hold uuids; pointers from find() are
// invalid after the next mutation.
class EntryStore : public QObject {
    Q_OBJECT

public:
    explicit EntryStore(QObject* parent = nullptr);

    // entryDir also becomes the root for later writes.
    QList<LoadError> load(const QString& entryDir);

    const QList<Entry>& all() const;
    const Entry* find(const QString& uuid) const;
    qsizetype count() const;

    // Assigns a uuid if missing. Returns it, or empty on failure.
    QString add(Entry entry);

    // Removes the entry and its folder from disk.
    bool remove(const QString& uuid);

    bool addTag(const QString& uuid, qsizetype imageIndex, const QString& tag);
    bool removeTag(const QString& uuid, qsizetype imageIndex, const QString& tag);
    bool removeImage(const QString& uuid, qsizetype imageIndex);

    bool setTitle(const QString& uuid, const QString& title);
    bool setComment(const QString& uuid, const QString& comment);
    bool setLora(const QString& uuid, const std::optional<Lora>& lora);

    QString folderFor(const QString& uuid) const;

signals:
    void reloaded();

    void entryAdded(const QString& uuid);
    void entryChanged(const QString& uuid);

    // Fires after removal.
    void entryRemoved(const QString& uuid);

    // Higher image indices shift down by one.
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
