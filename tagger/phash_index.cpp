#include <tagger/phash_index.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <bit>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

constexpr const char* kIndexFile = "__hashes.json";

// Stored as 16 hex characters rather than a number: QJsonValue holds a double,
// which cannot carry a 64-bit hash without rounding past 2^53.
QString toHex(uint64_t hash)
{
    return u"%1"_s.arg(hash, 16, 16, QChar(u'0'));
}

} // namespace

PHashIndex PHashIndex::load(const QString& collectionDir)
{
    PHashIndex index;

    QFile file(collectionDir + u"/"_s + QString::fromLatin1(kIndexFile));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return index;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray()) return index;

    for (const QJsonValue& value : doc.array()) {
        const QJsonObject object = value.toObject();
        const QString name = object.value(u"file"_s).toString();
        if (name.isEmpty()) continue;

        bool ok = false;
        const uint64_t hash = object.value(u"hash"_s).toString().toULongLong(&ok, 16);
        if (ok) index.m_byName.insert(name, hash);
    }
    return index;
}

bool PHashIndex::save(const QString& collectionDir) const
{
    QDir().mkpath(collectionDir);

    QJsonArray array;
    for (auto it = m_byName.cbegin(); it != m_byName.cend(); ++it)
        array.append(QJsonObject{{u"file"_s, it.key()}, {u"hash"_s, toHex(it.value())}});

    QFile file(collectionDir + u"/"_s + QString::fromLatin1(kIndexFile));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;

    file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
    return true;
}

qsizetype PHashIndex::size() const
{
    return m_byName.size();
}

bool PHashIndex::contains(const QString& filename) const
{
    return m_byName.contains(filename);
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
    int bestDistance = threshold + 1;

    for (auto it = m_byName.cbegin(); it != m_byName.cend(); ++it) {
        const int distance = int(std::popcount(hash ^ it.value()));
        if (distance >= bestDistance) continue;

        bestDistance = distance;
        best.filename = it.key();
        best.distance = distance;

        // Byte-identical, so nothing later can beat it.
        if (distance == 0) break;
    }

    return best.distance > threshold ? Match{} : best;
}

int PHashIndex::nextNumber() const
{
    int highest = 0;
    for (auto it = m_byName.cbegin(); it != m_byName.cend(); ++it) {
        const QString stem = QFileInfo(it.key()).completeBaseName();
        if (stem.size() < 5) continue;

        bool ok = false;
        const int number = stem.left(5).toInt(&ok);
        if (ok && number > highest) highest = number;
    }
    return highest + 1;
}

} // namespace tc
