#pragma once
#include <QList>
#include <QString>
#include <optional>


namespace core {

struct LoraConfig {
    // rootKey identifies which configured lora folder this file lives under:
    // "primary" (AppSettings::loraBaseDir) or "test" (AppSettings::loraTestDir).
    // file is the path relative to that root, no leading slash. Persisting
    // (rootKey, relPath) instead of an absolute path means changing the root
    // folder in settings reroots every entry without rewriting any of them.
    QString rootKey;
    QString file;
    double modelStr = 0.9;
    double clipStr = 2.0;
    QString sha256; // hex SHA-256 of the model file

    // Joins file against whichever root rootKey names. Returns empty when
    // the named root isn't configured.
    QString absolutePath(const QString& primaryDir, const QString& testDir) const;

    // Short label suitable for UI (basename without directory or extension).
    QString displayName() const;

    // If the resolved file doesn't exist on disk, scans the other root for a
    // same-relative-path match and rewrites rootKey on hit. Returns true if
    // anything changed - caller should persist the entry in that case.
    bool healAcrossRoots(const QString& primaryDir, const QString& testDir);
};

struct ImageData {
    QString fileName;
    QList<int32_t> tagIds;
};

struct Entry {
    int64_t creationTime{INT64_MAX};
    QString uuid;
    QString title;
    QString comment;
    QList<ImageData> images;
    uint32_t id{UINT32_MAX};
    bool modified{false};
    std::optional<LoraConfig> lora;
};

bool validEntry(const Entry& entry);

} // namespace core