#pragma once
#include <core/entry.h>
#include <core/load_error.h>
#include <QList>
#include <QString>
#include <expected>

namespace tc {

struct EntryLoad {
    QList<Entry> entries;
    QList<LoadError> errors;
};

// Reads <entryFolder>/__entry.json.
std::expected<Entry, LoadError> readEntry(const QString& entryFolder);

// Every immediate subdirectory of entryDir, in name order. One unreadable
// entry does not fail the rest -- it lands in errors instead.
EntryLoad readEntries(const QString& entryDir);

// Creates entryFolder if needed. Refuses an entry validEntry rejects.
std::expected<void, LoadError> writeEntry(const Entry& e, const QString& entryFolder);

} // namespace tc
