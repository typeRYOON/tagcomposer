#include <app/workflow_input_cache.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

constexpr auto kIndexFile = "_index.json";

} // namespace

WorkflowInputCache::WorkflowInputCache(const QString& cacheDir, QObject* parent)
    : QObject(parent), m_cacheDir(cacheDir)
{
    // No mkpath; importFromFile creates the folder.
    readIndex();
}

const QString& WorkflowInputCache::cacheDir() const
{
    return m_cacheDir;
}

QString WorkflowInputCache::serverSubfolder()
{
    return u"tagcomposer"_s;
}

QString WorkflowInputCache::importFromFile(const QString& sourcePath)
{
    const QFileInfo info(sourcePath);
    if (!info.exists() || !info.isFile()) return {};

    QImage image;
    QImageReader reader(sourcePath);
    reader.setAutoTransform(true);
    if (!reader.read(&image) || image.isNull()) return {};

    // ARGB so masks round-trip.
    if (image.format() != QImage::Format_ARGB32)
        image = image.convertToFormat(QImage::Format_ARGB32);

    if (!QDir().mkpath(m_cacheDir)) return {};

    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!image.save(m_cacheDir + u"/"_s + uuid + u".png"_s, "PNG")) return {};

    m_byUuid.insert(uuid, WorkflowInput{uuid, info.completeBaseName(), info.fileName(),
                                        image.width(), image.height()});
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
    // The file may have been deleted externally.
    return QFile::exists(localPath(uuid));
}

QString WorkflowInputCache::localPath(const QString& uuid) const
{
    return m_cacheDir + u"/"_s + uuid + u".png"_s;
}

QString WorkflowInputCache::serverFilename(const QString& uuid) const
{
    return serverSubfolder() + u"/"_s + uuid + u".png"_s;
}

QString WorkflowInputCache::maskPath(const QString& maskId) const
{
    return m_cacheDir + u"/_masks/"_s + maskId + u".png"_s;
}

QString WorkflowInputCache::saveMask(const QImage& mask)
{
    if (mask.isNull()) return {};

    const QString dir = m_cacheDir + u"/_masks"_s;
    if (!QDir().mkpath(dir)) return {};

    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QImage grey = mask.format() == QImage::Format_Grayscale8
        ? mask
        : mask.convertToFormat(QImage::Format_Grayscale8);

    if (!grey.save(dir + u"/"_s + id + u".png"_s, "PNG")) return {};
    return id;
}

QImage WorkflowInputCache::loadMask(const QString& maskId) const
{
    if (maskId.isEmpty()) return {};

    QImage mask(maskPath(maskId));
    if (mask.isNull()) return mask;
    if (mask.format() != QImage::Format_Grayscale8)
        mask = mask.convertToFormat(QImage::Format_Grayscale8);
    return mask;
}

void WorkflowInputCache::removeMask(const QString& maskId)
{
    if (maskId.isEmpty()) return;
    QFile::remove(maskPath(maskId));
}

QString WorkflowInputCache::resolveEdited(const QString& uuid, const ImageEdits& edits) const
{
    if (!edits.enabled || uuid.isEmpty()) return localPath(uuid);

    const QString source = localPath(uuid);
    const QString editedDir = m_cacheDir + u"/_edited/"_s + edits.hash();
    const QString editedPath = editedDir + u"/"_s + uuid + u".png"_s;
    if (QFile::exists(editedPath)) return editedPath;

    QImage image(source);
    if (image.isNull()) return source;
    if (image.format() != QImage::Format_ARGB32)
        image = image.convertToFormat(QImage::Format_ARGB32);

    // Edits saved against a larger image survive a smaller re-import.
    QRect crop = edits.cropRect.intersected(image.rect());
    if (crop.isEmpty()) crop = image.rect();

    // LoadImage reads RGB as IMAGE and (1 - alpha) as MASK. trimToCrop: the crop at
    // full alpha. Otherwise source-sized, alpha from the mask (or the crop rect).
    QImage out = edits.trimToCrop ? image.copy(crop) : image.copy();
    if (out.format() != QImage::Format_ARGB32)
        out = out.convertToFormat(QImage::Format_ARGB32);

    if (edits.trimToCrop) {
        for (int y = 0; y < out.height(); ++y) {
            auto* row = reinterpret_cast<QRgb*>(out.scanLine(y));
            for (int x = 0; x < out.width(); ++x)
                row[x] = qRgba(qRed(row[x]), qGreen(row[x]), qBlue(row[x]), 255);
        }
    } else {
        const QImage mask = loadMask(edits.maskId);
        const bool useMask = !mask.isNull() && mask.size() == image.size();

        for (int y = 0; y < out.height(); ++y) {
            auto* row = reinterpret_cast<QRgb*>(out.scanLine(y));
            const uchar* maskRow = useMask ? mask.constScanLine(y) : nullptr;
            const bool inRows = y >= crop.top() && y <= crop.bottom();

            for (int x = 0; x < out.width(); ++x) {
                int alpha = 255;
                if (useMask)
                    alpha = 255 - maskRow[x]; // mask 255 becomes alpha 0
                else if (inRows && x >= crop.left() && x <= crop.right())
                    alpha = 0;
                row[x] = qRgba(qRed(row[x]), qGreen(row[x]), qBlue(row[x]), alpha);
            }
        }
    }

    if (!QDir().mkpath(editedDir)) return source;
    if (!out.save(editedPath, "PNG")) return source;
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

    QFile file(m_cacheDir + u"/"_s + QString::fromLatin1(kIndexFile));
    if (!file.open(QIODevice::ReadOnly)) return;

    const QJsonArray entries =
        QJsonDocument::fromJson(file.readAll()).object()[u"entries"_s].toArray();

    for (const QJsonValue entry : entries) {
        const QJsonObject obj = entry.toObject();
        WorkflowInput record;
        record.uuid = obj[u"uuid"_s].toString();
        record.displayName = obj[u"displayName"_s].toString();
        record.originalFile = obj[u"originalFile"_s].toString();
        record.width = obj[u"width"_s].toInt();
        record.height = obj[u"height"_s].toInt();
        if (!record.uuid.isEmpty()) m_byUuid.insert(record.uuid, record);
    }
}

void WorkflowInputCache::writeIndex() const
{
    QJsonArray entries;
    for (const WorkflowInput& record : m_byUuid) {
        QJsonObject obj;
        obj[u"uuid"_s] = record.uuid;
        obj[u"displayName"_s] = record.displayName;
        obj[u"originalFile"_s] = record.originalFile;
        obj[u"width"_s] = record.width;
        obj[u"height"_s] = record.height;
        entries.append(obj);
    }

    QJsonObject root;
    root[u"entries"_s] = entries;

    QFile file(m_cacheDir + u"/"_s + QString::fromLatin1(kIndexFile));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

} // namespace tc
