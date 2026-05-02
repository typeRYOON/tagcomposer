#include <core/workflowinputcache.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace core {

namespace {
constexpr const char* kIndexFile = "_index.json";
}

WorkflowInputCache::WorkflowInputCache(const QString& cacheDir, QObject* parent)
    : QObject(parent), m_cacheDir(cacheDir)
{
    QDir().mkpath(m_cacheDir);
    readIndex();
}

QString WorkflowInputCache::importFromFile(const QString& srcPath)
{
    QFileInfo fi(srcPath);
    if (!fi.exists() || !fi.isFile()) return {};

    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString dst  = m_cacheDir + "/" + uuid + ".png";

    QImage img;
    QImageReader reader(srcPath);
    reader.setAutoTransform(true);
    if (!reader.read(&img) || img.isNull()) return {};

    // Always store as ARGB so the alpha (mask) round-trip is lossless.
    if (img.format() != QImage::Format_ARGB32)
        img = img.convertToFormat(QImage::Format_ARGB32);

    if (!img.save(dst, "PNG")) return {};

    WorkflowInput rec;
    rec.uuid         = uuid;
    rec.displayName  = fi.completeBaseName();
    rec.originalFile = fi.fileName();
    rec.width        = img.width();
    rec.height       = img.height();

    m_byUuid.insert(uuid, rec);
    writeIndex();
    emit added(uuid);
    return uuid;
}

void WorkflowInputCache::remove(const QString& uuid)
{
    if (!m_byUuid.contains(uuid)) return;
    QFile::remove(localPath(uuid));
    m_byUuid.remove(uuid);
    writeIndex();
    emit removed(uuid);
}

bool WorkflowInputCache::has(const QString& uuid) const
{
    if (uuid.isEmpty() || !m_byUuid.contains(uuid)) return false;
    // Index can drift from disk if the file was deleted manually; reflect that.
    return QFile::exists(localPath(uuid));
}

QString WorkflowInputCache::localPath(const QString& uuid) const
{
    return m_cacheDir + "/" + uuid + ".png";
}

QString WorkflowInputCache::serverFilename(const QString& uuid) const
{
    return serverSubfolder() + "/" + uuid + ".png";
}

QList<WorkflowInput> WorkflowInputCache::all() const
{
    return m_byUuid.values();
}

WorkflowInput WorkflowInputCache::get(const QString& uuid) const
{
    return m_byUuid.value(uuid);
}

void WorkflowInputCache::readIndex()
{
    m_byUuid.clear();
    QFile f(m_cacheDir + "/" + kIndexFile);
    if (!f.open(QIODevice::ReadOnly)) return;

    const QJsonArray arr =
        QJsonDocument::fromJson(f.readAll()).object()["entries"].toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        WorkflowInput rec;
        rec.uuid         = o["uuid"].toString();
        rec.displayName  = o["displayName"].toString();
        rec.originalFile = o["originalFile"].toString();
        rec.width        = o["width"].toInt();
        rec.height       = o["height"].toInt();
        if (!rec.uuid.isEmpty())
            m_byUuid.insert(rec.uuid, rec);
    }
}

void WorkflowInputCache::writeIndex() const
{
    QJsonArray arr;
    for (const WorkflowInput& rec : m_byUuid) {
        QJsonObject o;
        o["uuid"]         = rec.uuid;
        o["displayName"]  = rec.displayName;
        o["originalFile"] = rec.originalFile;
        o["width"]        = rec.width;
        o["height"]       = rec.height;
        arr.append(o);
    }
    QJsonObject root;
    root["entries"] = arr;

    QFile f(m_cacheDir + "/" + kIndexFile);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

} // namespace core
