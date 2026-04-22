#pragma once
#include <core/entry.h>
#include <core/tagindex.h>
#include <QList>

namespace io {
    class EntryIO {
    public:
        EntryIO() = delete;
        static QList<core::Entry> loadAll(
            core::TagIndex& tagIndex,
            const QString& basePath,
            const core::EntryType entryType
        );
        static void save(const core::Entry& e);
    };
}