#pragma once
#include <core/entry.h>

namespace core {

    bool validEntry(const Entry& entry)
    {
        if (entry.creationTime == INT64_MAX
            || entry.id == INT32_MAX
            || entry.uuid.isEmpty()
            || entry.images.isEmpty()
            || entry.title.isEmpty()
            ) {
            return false;
        }
        return true;
    }

}