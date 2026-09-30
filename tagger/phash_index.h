#pragma once
#include <QHash>
#include <QString>
#include <cstdint>

namespace tc {

// The <filename, pHash> map for one collection, kept in
// `<collectionDir>/__hashes.json`.
//
// Lookup is a linear popcount scan. That is fine to roughly fifty thousand
// entries, and a tree over Hamming distance would cost more to maintain than
// the scan costs to run at this size.
class PHashIndex {
public:
    struct Match {
        QString filename; // empty when nothing was near enough
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
