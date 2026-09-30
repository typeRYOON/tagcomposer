#include <core/entry.h>
#include <QDir>
#include <QFileInfo>

using namespace Qt::Literals::StringLiterals;

namespace tc {

QString Lora::absolutePath(const QString& primaryDir, const QString& testDir) const
{
    if (file.isEmpty()) return {};
    const QString root = (rootKey == QLatin1String("test")) ? testDir : primaryDir;
    if (root.isEmpty()) return {};
    return QDir::cleanPath(root + QLatin1Char('/') + file);
}

QString Lora::displayName() const
{
    return QFileInfo(file).completeBaseName();
}

bool validEntry(const Entry& e)
{
    return !e.uuid.isEmpty() && !e.title.isEmpty() && !e.images.isEmpty();
}

QString normalizeTag(QString tag)
{
    tag.replace("\\("_L1, "("_L1);
    tag.replace("\\)"_L1, ")"_L1);
    tag.replace(u'_', u' ');
    return tag.trimmed().toLower();
}

QString serializeTag(QString tag)
{
    tag.replace("("_L1, "\\("_L1);
    tag.replace(")"_L1, "\\)"_L1);
    tag.replace(u' ', u'_');
    return tag.toLower();
}

} // namespace tc
