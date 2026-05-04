#pragma once
#include <QList>
#include <QString>
#include <optional>


namespace core {

struct LoraConfig {
    QString file; // absolute path on disk
    double modelStr = 0.9;
    double clipStr = 2.0;
    QString sha256; // hex SHA-256 of the model file
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