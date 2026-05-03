#pragma once
#include <QString>
#include <QHash>
#include <QList>
#include <cstdint>

namespace core {

// Persistent map of <filename → 64-bit pHash> for one collection. Backed by
// `<collectionDir>/__hashes.json`:
//
//   [
//     { "file": "00001.png", "hash": "deadbeefcafebabe" },
//     ...
//   ]
//
// Hash is stored as a 16-char zero-padded lowercase hex string so we don't
// lose precision through QJsonDocument's double-backed numeric type
// (which can't faithfully round-trip values above 2^53).
//
// Lookup is a linear popcount scan — for collections under ~50k entries
// that's sub-millisecond. If we ever need more, swap to a BKTree (the
// imagehasher.py reference has one); the public API doesn't have to change.
class PHashIndex {
public:
    struct Match {
        QString  filename;     // empty when no match was found
        int      distance = -1;
    };

    static PHashIndex loadFromDir(const QString& collectionDir);
    void              saveToDir(const QString& collectionDir) const;

    int    size() const { return int(m_byName.size()); }
    bool   contains(const QString& filename) const { return m_byName.contains(filename); }

    void   add(const QString& filename, uint64_t hash);
    void   remove(const QString& filename);
    void   clear();

    // Closest match within `threshold` Hamming bits. `Match::filename` is
    // empty if nothing is within threshold.
    Match  findNearest(uint64_t hash, int threshold) const;

    // Highest "NNNNN.<ext>" prefix in the index plus 1, so the watcher can
    // hand out the next sequential number without rescanning the dir. Drops
    // back to 1 when the index is empty.
    int    nextNumber() const;

private:
    // QHash so contains/remove are O(1); the linear hamming scan iterates
    // values directly.
    QHash<QString, uint64_t> m_byName;
};

} // namespace core
