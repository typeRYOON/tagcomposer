#include <core/entry.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace core {

bool validEntry(const Entry& entry)
{
    // id is excluded: validEntry runs in loadOne before the caller assigns one.
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
