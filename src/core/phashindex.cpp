#include <core/phashindex.h>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

namespace core {

namespace {
constexpr const char* kIndexFilename = "__hashes.json";

QString hashToHex(uint64_t h)
{
    return QString("%1").arg(h, 16, 16, QChar('0'));
}

uint64_t hexToHash(const QString& s, bool* ok)
{
    return s.toULongLong(ok, 16);
}
} // namespace

PHashIndex PHashIndex::loadFromDir(const QString& collectionDir)
{
    PHashIndex idx;

    QFile f(collectionDir + "/" + kIndexFilename);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return idx;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) return idx;

    for (const QJsonValue& v : doc.array()) {
        const QJsonObject o = v.toObject();
        const QString name = o.value("file").toString();
        if (name.isEmpty()) continue;

        bool ok = false;
        const uint64_t h = hexToHash(o.value("hash").toString(), &ok);
        if (!ok) continue;

        idx.m_byName.insert(name, h);
    }
    return idx;
}

void PHashIndex::saveToDir(const QString& collectionDir) const
{
    QDir().mkpath(collectionDir);

    QJsonArray arr;
    for (auto it = m_byName.constBegin(); it != m_byName.constEnd(); ++it) {
        QJsonObject o;
        o["file"] = it.key();
        o["hash"] = hashToHex(it.value());
        arr.append(o);
    }

    QFile f(collectionDir + "/" + kIndexFilename);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void PHashIndex::add(const QString& filename, uint64_t hash)
{
    m_byName.insert(filename, hash);
}

void PHashIndex::remove(const QString& filename)
{
    m_byName.remove(filename);
}

void PHashIndex::clear()
{
    m_byName.clear();
}

PHashIndex::Match PHashIndex::findNearest(uint64_t hash, int threshold) const
{
    Match best;
    int bestDist = threshold + 1;

    for (auto it = m_byName.constBegin(); it != m_byName.constEnd(); ++it) {
        const int d = std::popcount(hash ^ it.value());
        if (d < bestDist) {
            bestDist = d;
            best.filename = it.key();
            best.distance = d;
            if (d == 0) break; // can't get closer
        }
    }
    if (best.distance > threshold) return {};
    return best;
}

int PHashIndex::nextNumber() const
{
    int high = 0;
    for (auto it = m_byName.constBegin(); it != m_byName.constEnd(); ++it) {
        // "NNNNN.ext" → take the basename's first 5 digits.
        const QString stem = QFileInfo(it.key()).completeBaseName();
        if (stem.size() < 5) continue;
        bool ok = false;
        const int n = stem.left(5).toInt(&ok);
        if (ok && n > high) high = n;
    }
    return high + 1;
}

} // namespace core
