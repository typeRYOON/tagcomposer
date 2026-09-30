#include <core/entry_store.h>
#include <core/entry_io.h>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QUuid>

using namespace Qt::StringLiterals;

namespace tc {

EntryStore::EntryStore(QObject* parent) : QObject(parent) {}

QList<LoadError> EntryStore::load(const QString& entryDir)
{
    m_root = entryDir;

    EntryLoad loaded = readEntries(entryDir);
    m_entries = std::move(loaded.entries);
    reindex();

    emit reloaded();
    return loaded.errors;
}

void EntryStore::reindex()
{
    m_indexByUuid.clear();
    m_indexByUuid.reserve(m_entries.size());
    for (qsizetype i = 0; i < m_entries.size(); ++i)
        m_indexByUuid.insert(m_entries[i].uuid, i);
}

const QList<Entry>& EntryStore::all() const
{
    return m_entries;
}

qsizetype EntryStore::count() const
{
    return m_entries.size();
}

const Entry* EntryStore::find(const QString& uuid) const
{
    const auto it = m_indexByUuid.constFind(uuid);
    if (it == m_indexByUuid.constEnd()) return nullptr;
    return &m_entries[*it];
}

Entry* EntryStore::mutableEntry(const QString& uuid)
{
    const auto it = m_indexByUuid.constFind(uuid);
    if (it == m_indexByUuid.constEnd()) return nullptr;
    return &m_entries[*it];
}

QString EntryStore::folderFor(const QString& uuid) const
{
    if (m_root.isEmpty() || uuid.isEmpty()) return {};
    return m_root + u"/"_s + uuid;
}

bool EntryStore::persist(const Entry& entry)
{
    const std::expected<void, LoadError> written = writeEntry(entry, folderFor(entry.uuid));
    if (written) return true;

    emit writeFailed(entry.uuid, written.error().reason);
    return false;
}

QString EntryStore::add(Entry entry)
{
    if (entry.uuid.isEmpty()) entry.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (entry.created == 0) entry.created = QDateTime::currentSecsSinceEpoch();
    if (!validEntry(entry)) return {};
    if (m_indexByUuid.contains(entry.uuid)) return {};

    if (!persist(entry)) return {};

    const QString uuid = entry.uuid;
    m_indexByUuid.insert(uuid, m_entries.size());
    m_entries << std::move(entry);

    emit entryAdded(uuid);
    return uuid;
}

bool EntryStore::remove(const QString& uuid)
{
    const auto it = m_indexByUuid.constFind(uuid);
    if (it == m_indexByUuid.constEnd()) return false;

    // Copied, because uuid may reference the entry about to be erased.
    const QString id = uuid;
    const QString folder = folderFor(id);

    m_entries.removeAt(*it);
    reindex();

    if (!folder.isEmpty()) QDir(folder).removeRecursively();

    emit entryRemoved(id);
    return true;
}

bool EntryStore::addTag(const QString& uuid, qsizetype imageIndex, const QString& tag)
{
    Entry* entry = mutableEntry(uuid);
    if (!entry || imageIndex < 0 || imageIndex >= entry->images.size()) return false;

    const QString trimmed = tag.trimmed();
    if (trimmed.isEmpty()) return false;

    QStringList& tags = entry->images[imageIndex].tags;
    if (tags.contains(trimmed)) return false;

    tags << trimmed;
    if (!persist(*entry)) return false;

    emit entryChanged(uuid);
    return true;
}

bool EntryStore::removeTag(const QString& uuid, qsizetype imageIndex, const QString& tag)
{
    Entry* entry = mutableEntry(uuid);
    if (!entry || imageIndex < 0 || imageIndex >= entry->images.size()) return false;

    QStringList& tags = entry->images[imageIndex].tags;
    if (tags.removeAll(tag) == 0) return false;

    if (!persist(*entry)) return false;

    emit entryChanged(uuid);
    return true;
}

bool EntryStore::removeImage(const QString& uuid, qsizetype imageIndex)
{
    Entry* entry = mutableEntry(uuid);
    if (!entry || imageIndex < 0 || imageIndex >= entry->images.size()) return false;

    const QString fileName = entry->images[imageIndex].fileName;
    entry->images.removeAt(imageIndex);

    // An entry with no images left is no longer storable, so it goes entirely
    // rather than leaving a folder writeEntry would refuse.
    if (entry->images.isEmpty()) {
        QFile::remove(folderFor(uuid) + u"/"_s + fileName);
        return remove(uuid);
    }

    if (!persist(*entry)) return false;
    QFile::remove(folderFor(uuid) + u"/"_s + fileName);

    emit imageRemoved(uuid, imageIndex);
    emit entryChanged(uuid);
    return true;
}

bool EntryStore::setTitle(const QString& uuid, const QString& title)
{
    Entry* entry = mutableEntry(uuid);
    if (!entry || title.isEmpty() || entry->title == title) return false;

    entry->title = title;
    if (!persist(*entry)) return false;

    emit entryChanged(uuid);
    return true;
}

bool EntryStore::setComment(const QString& uuid, const QString& comment)
{
    Entry* entry = mutableEntry(uuid);
    if (!entry || entry->comment == comment) return false;

    entry->comment = comment;
    if (!persist(*entry)) return false;

    emit entryChanged(uuid);
    return true;
}

bool EntryStore::setLora(const QString& uuid, const std::optional<Lora>& lora)
{
    Entry* entry = mutableEntry(uuid);
    if (!entry || entry->lora == lora) return false;

    entry->lora = lora;
    if (!persist(*entry)) return false;

    emit entryChanged(uuid);
    return true;
}

} // namespace tc
