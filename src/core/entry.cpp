#include <core/entry.h>

namespace core {

bool validEntry(const Entry& entry)
{
    // id is intentionally not checked: validEntry runs inside EntryIO::loadOne
    // before the caller assigns a runtime id, so the field is still the
    // default sentinel at this point. The other fields all come straight
    // from the parsed JSON, so their default values do flag malformed input.
    if (entry.creationTime == INT64_MAX || entry.uuid.isEmpty() || entry.images.isEmpty() ||
        entry.title.isEmpty()) {
        return false;
    }
    return true;
}

} // namespace core
