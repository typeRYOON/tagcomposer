#pragma once
#include <QList>
#include <QString>
#include <QStringList>
#include <QtTypes>
#include <optional>

namespace tc {

// Relative to a named root, so changing a root in settings moves every LoRA.
struct Lora {
    QString rootKey; // "primary" or "test"
    QString file;    // relative to that root
    QString sha256;
    double modelStrength = 1.0;
    double clipStrength = 1.0;

    QString absolutePath(const QString& primaryDir, const QString& testDir) const;
    QString displayName() const;

    bool operator==(const Lora&) const = default;
};

struct EntryImage {
    QString fileName; // relative to the entry folder
    QStringList tags;
};

struct Entry {
    QString uuid; // also the folder name
    QString title;
    QString comment;
    qint64 created = 0; // unix seconds
    QList<EntryImage> images;
    std::optional<Lora> lora;
};

// Needs a uuid, a title and at least one image.
bool validEntry(const Entry& e);

// In-app form: spaces, unescaped parens, lower case. Apply on every read.
QString normalizeTag(QString tag);

// Underscores and backslash-escaped parens.
QString serializeTag(QString tag);

} // namespace tc
