#include <core/workflowinputcache.h>
#include <core/workflowmanager.h> // ImageEdits
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
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
    const QString dst = m_cacheDir + "/" + uuid + ".png";

    QImage img;
    QImageReader reader(srcPath);
    reader.setAutoTransform(true);
    if (!reader.read(&img) || img.isNull()) return {};

    // ARGB so alpha (mask) round-trip is lossless.
    if (img.format() != QImage::Format_ARGB32) img = img.convertToFormat(QImage::Format_ARGB32);

    if (!img.save(dst, "PNG")) return {};

    WorkflowInput rec;
    rec.uuid = uuid;
    rec.displayName = fi.completeBaseName();
    rec.originalFile = fi.fileName();
    rec.width = img.width();
    rec.height = img.height();

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
    // Index can drift if a file was deleted out-of-band.
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

QString WorkflowInputCache::maskPath(const QString& maskId) const
{
    return m_cacheDir + "/_masks/" + maskId + ".png";
}

QString WorkflowInputCache::saveMask(const QImage& mask)
{
    if (mask.isNull()) return {};
    const QString dir = m_cacheDir + "/_masks";
    if (!QDir().mkpath(dir)) return {};
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString path = dir + "/" + id + ".png";
    QImage saved = mask.format() == QImage::Format_Grayscale8
                       ? mask
                       : mask.convertToFormat(QImage::Format_Grayscale8);
    if (!saved.save(path, "PNG")) return {};
    return id;
}

QImage WorkflowInputCache::loadMask(const QString& maskId) const
{
    if (maskId.isEmpty()) return {};
    QImage img(maskPath(maskId));
    if (img.isNull()) return img;
    if (img.format() != QImage::Format_Grayscale8)
        img = img.convertToFormat(QImage::Format_Grayscale8);
    return img;
}

void WorkflowInputCache::removeMask(const QString& maskId)
{
    if (maskId.isEmpty()) return;
    QFile::remove(maskPath(maskId));
}

QString WorkflowInputCache::resolveEdited(const QString& uuid, const ImageEdits& edits) const
{
    if (!edits.enabled || uuid.isEmpty()) return localPath(uuid);

    const QString src = localPath(uuid);
    const QString editedDir = m_cacheDir + "/_edited/" + edits.hash();
    // Basename must stay <uuid>.png so the server-side path stays
    // `tagcomposer/<uuid>.png` regardless of which variant was uploaded.
    const QString editedPath = editedDir + "/" + uuid + ".png";
    if (QFile::exists(editedPath)) return editedPath;

    QImage source(src);
    if (source.isNull()) return src;
    if (source.format() != QImage::Format_ARGB32)
        source = source.convertToFormat(QImage::Format_ARGB32);

    // Clamp against stale edits from a re-imported smaller image.
    QRect crop = edits.cropRect.intersected(source.rect());
    if (crop.isEmpty()) crop = source.rect();

    // ComfyUI's LoadImage reads RGB as IMAGE, (1 - alpha) as MASK.
    //   trimToCrop=true:  output is the crop, alpha=255 (no mask).
    //   trimToCrop=false: source-size, alpha encodes the painted mask
    //                     (or rect-as-mask fallback when none).
    QImage out = edits.trimToCrop ? source.copy(crop) : source.copy();
    if (out.format() != QImage::Format_ARGB32) out = out.convertToFormat(QImage::Format_ARGB32);

    if (edits.trimToCrop) {
        for (int y = 0; y < out.height(); ++y) {
            QRgb* row = reinterpret_cast<QRgb*>(out.scanLine(y));
            for (int x = 0; x < out.width(); ++x) {
                const QRgb px = row[x];
                row[x] = qRgba(qRed(px), qGreen(px), qBlue(px), 255);
            }
        }
    }
    else {
        // Painted mask wins; rect-as-mask is the legacy fallback.
        QImage mask = loadMask(edits.maskId);
        const bool useMask = !mask.isNull() && mask.size() == source.size();

        for (int y = 0; y < out.height(); ++y) {
            QRgb* row = reinterpret_cast<QRgb*>(out.scanLine(y));
            const uchar* maskRow = useMask ? mask.constScanLine(y) : nullptr;
            const bool yInRect = (y >= crop.top() && y <= crop.bottom());
            for (int x = 0; x < out.width(); ++x) {
                int alpha = 255;
                if (useMask) {
                    alpha = 255 - maskRow[x]; // mask 255 -> alpha 0
                }
                else if (yInRect && x >= crop.left() && x <= crop.right()) {
                    alpha = 0;
                }
                const QRgb px = row[x];
                row[x] = qRgba(qRed(px), qGreen(px), qBlue(px), alpha);
            }
        }
    }

    if (!QDir().mkpath(editedDir)) return src;
    if (!out.save(editedPath, "PNG")) return src;
    return editedPath;
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

    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).object()["entries"].toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        WorkflowInput rec;
        rec.uuid = o["uuid"].toString();
        rec.displayName = o["displayName"].toString();
        rec.originalFile = o["originalFile"].toString();
        rec.width = o["width"].toInt();
        rec.height = o["height"].toInt();
        if (!rec.uuid.isEmpty()) m_byUuid.insert(rec.uuid, rec);
    }
}

void WorkflowInputCache::writeIndex() const
{
    QJsonArray arr;
    for (const WorkflowInput& rec : m_byUuid) {
        QJsonObject o;
        o["uuid"] = rec.uuid;
        o["displayName"] = rec.displayName;
        o["originalFile"] = rec.originalFile;
        o["width"] = rec.width;
        o["height"] = rec.height;
        arr.append(o);
    }
    QJsonObject root;
    root["entries"] = arr;

    QFile f(m_cacheDir + "/" + kIndexFile);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

} // namespace core
