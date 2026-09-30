#pragma once
#include <QList>
#include <QString>
#include <QStringList>
#include <QtTypes>
#include <optional>

namespace tc {

// Stored relative to a named root instead of by absolute path, so changing a
// root in settings re-roots every entry at once.
struct Lora {
    QString rootKey; // "primary" or "test"; resolved against the settings dirs
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
    QString uuid; // identity, and the folder name on disk
    QString title;
    QString comment;
    qint64 created = 0; // unix seconds
    QList<EntryImage> images;
    std::optional<Lora> lora;
};

// Storable only with an identity, a title, and at least one image.
bool validEntry(const Entry& e);

// The in-app form of a tag: spaces, unescaped parens, lower case. Everything
// that reads a tag from a file or an API runs it through this first, so one
// tag has exactly one spelling everywhere.
QString normalizeTag(QString tag);

// The Danbooru wire form: underscores and escaped parens.
QString serializeTag(QString tag);

} // namespace tc
