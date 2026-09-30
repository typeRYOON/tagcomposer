#pragma once
#include <QHash>
#include <QString>
#include <cstdint>

namespace tc {

// <filename, pHash> map for one collection, stored in <collectionDir>/__hashes.json.
// Lookup is a linear scan, fine up to ~50k entries.
class PHashIndex {
public:
    struct Match {
        QString filename; // empty if no match
        int distance = -1;
    };

    static PHashIndex load(const QString& collectionDir);
    bool save(const QString& collectionDir) const;

    qsizetype size() const;
    bool contains(const QString& filename) const;

    void add(const QString& filename, uint64_t hash);
    void remove(const QString& filename);
    void clear();

    Match findNearest(uint64_t hash, int threshold) const;

    // One past the highest "NNNNN.*" prefix, or 1 when the index is empty.
    int nextNumber() const;

private:
    QHash<QString, uint64_t> m_byName;
};

} // namespace tc
