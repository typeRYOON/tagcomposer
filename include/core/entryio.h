#pragma once
#include <core/entry.h>
#include <core/tagindex.h>
#include <QList>
#include <optional>

namespace core {
class EntryIO {
public:
    EntryIO() = delete;
    static QList<Entry> loadAll(TagIndex& tagIndex, const QString& basePath);
    // Parse a single entry folder (containing __entry.json + image files).
    // Returns nullopt if the JSON is missing required fields. The returned
    // Entry has a default id; the caller (typically EntryModel::addEntry)
    // assigns the runtime id.
    static std::optional<Entry> loadOne(const QString& entryFolder, TagIndex& tagIndex);
    static void save(const Entry& e, const TagIndex& tagIndex);
};
} // namespace core
