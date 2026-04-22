#pragma once
#include <QList>


namespace core {

    enum class EntryType : unsigned char {
        Concept,
        Model,
        None
    };

    struct ImageData {
        QString fileName;
        QList<int32_t> tagIds;
    };

    struct Entry {
        int64_t creationTime{ INT64_MAX };
        QString uuid;
        QString title;
        QList<ImageData> images;
        uint32_t id{ UINT32_MAX };
        EntryType type{ EntryType::None };
    };

    bool validEntry(const Entry& entry);

}