#pragma once
#include <QString>
#include <QHash>
#include <QList>
#include <cstdint>

namespace core {

// Persistent <filename -> pHash> map for one collection, backed by
// `<collectionDir>/__hashes.json` (hash stored as 16-hex chars to dodge
// QJsonDocument's 2^53 numeric limit). Lookup is a linear popcount scan;
// fine to ~50k entries.
class PHashIndex {
public:
    struct Match {
        QString filename; // empty when no match
        int distance = -1;
    };

    static PHashIndex loadFromDir(const QString& collectionDir);
    void saveToDir(const QString& collectionDir) const;

    int size() const
    {
        return int(m_byName.size());
    }
    bool contains(const QString& filename) const
    {
        return m_byName.contains(filename);
    }

    void add(const QString& filename, uint64_t hash);
    void remove(const QString& filename);
    void clear();

    Match findNearest(uint64_t hash, int threshold) const;

    // Highest "NNNNN.*" prefix in the index, plus 1. Returns 1 if empty.
    int nextNumber() const;

private:
    QHash<QString, uint64_t> m_byName;
};

} // namespace core
