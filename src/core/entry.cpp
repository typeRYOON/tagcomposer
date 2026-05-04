#include <core/entry.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>

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

QString LoraConfig::absolutePath(const QString& primaryDir, const QString& testDir) const
{
    if (file.isEmpty()) return {};
    const QString root = (rootKey == QLatin1String("test")) ? testDir : primaryDir;
    if (root.isEmpty()) return {};
    return QDir::cleanPath(root + QLatin1Char('/') + file);
}

QString LoraConfig::displayName() const
{
    return QFileInfo(file).completeBaseName();
}

bool LoraConfig::healAcrossRoots(const QString& primaryDir, const QString& testDir)
{
    if (file.isEmpty() || rootKey.isEmpty()) return false;

    const QString currentAbs = absolutePath(primaryDir, testDir);
    if (!currentAbs.isEmpty() && QFile::exists(currentAbs)) return false;

    const QString otherKey = (rootKey == QLatin1String("primary")) ? QLatin1String("test")
                                                                   : QLatin1String("primary");
    const QString otherDir = (rootKey == QLatin1String("primary")) ? testDir : primaryDir;
    if (otherDir.isEmpty()) return false;

    const QString candidate = QDir::cleanPath(otherDir + QLatin1Char('/') + file);
    if (QFile::exists(candidate)) {
        rootKey = otherKey;
        return true;
    }
    return false;
}

} // namespace core
