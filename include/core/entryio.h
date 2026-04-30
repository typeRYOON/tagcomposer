#pragma once
#include <core/entry.h>
#include <core/tagindex.h>
#include <QList>

namespace core {
    class EntryIO {
    public:
        EntryIO() = delete;
        static QList<Entry> loadAll(
            TagIndex& tagIndex,
            const QString& basePath
        );
        static void save(const Entry& e, const TagIndex& tagIndex);
    };
}
