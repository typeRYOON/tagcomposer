#pragma once
#include <QList>
#include <QString>
#include <optional>


namespace core {

struct LoraConfig {
    // rootKey is "primary" (AppSettings::loraBaseDir) or "test"
    // (loraTestDir); file is relative to that root. Decoupling from absolute
    // paths means changing a root in settings reroots every entry for free.
    QString rootKey;
    QString file;
    double modelStr = 0.9;
    double clipStr = 2.0;
    QString sha256;

    QString absolutePath(const QString& primaryDir, const QString& testDir) const;
    QString displayName() const;

    // If the file no longer resolves under rootKey but the same relative
    // path exists under the other root, swap rootKey. Returns true on swap;
    // caller persists the entry.
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