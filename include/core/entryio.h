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
    // Returns nullopt for missing/invalid __entry.json. id is left default;
    // the caller assigns the runtime id.
    static std::optional<Entry> loadOne(const QString& entryFolder, TagIndex& tagIndex);
    static void save(const Entry& e, const TagIndex& tagIndex);
};
} // namespace core
