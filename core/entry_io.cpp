#include <core/entry_io.h>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

namespace tc {
namespace {

constexpr int kFormatVersion = 1;
constexpr QLatin1StringView kEntryFile{"__entry.json"};

std::optional<Lora> loraFromJson(const QJsonValue& v)
{
    if (!v.isObject()) return std::nullopt;

    const QJsonObject o = v.toObject();
    Lora l;
    l.rootKey = o["rootKey"].toString();
    l.file = o["file"].toString();
    l.sha256 = o["sha256"].toString();
    l.modelStrength = o["modelStr"].toDouble(1.0);
    l.clipStrength = o["clipStr"].toDouble(1.0);

    if (l.file.isEmpty()) return std::nullopt;
    return l;
}

QJsonObject loraToJson(const Lora& l)
{
    QJsonObject o;
    o["rootKey"] = l.rootKey;
    o["file"] = l.file;
    o["sha256"] = l.sha256;
    o["modelStr"] = l.modelStrength;
    o["clipStr"] = l.clipStrength;
    return o;
}

} // namespace

std::expected<Entry, LoadError> readEntry(const QString& entryFolder)
{
    const QString path = entryFolder + u'/' + kEntryFile;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::unexpected(LoadError{path, "cannot open: " + f.errorString()});

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError) {
        return std::unexpected(LoadError{
            path, QString("invalid json at offset %1: %2").arg(perr.offset).arg(perr.errorString())});
    }
    if (!doc.isObject()) return std::unexpected(LoadError{path, "root is not an object"});

    const QJsonObject obj = doc.object();

    Entry e;
    e.uuid = obj["uuid"].toString();
    e.title = obj["title"].toString();
    e.comment = obj["comment"].toString();
    e.created = obj["creation"].toInteger();

    const QJsonArray images = obj["images"].toArray();
    e.images.reserve(images.size());
    for (const QJsonValue img : images) {
        const QJsonObject io = img.toObject();
        if (!io.contains("file")) continue;

        EntryImage out;
        out.fileName = io["file"].toString();
        const QJsonArray tags = io["tags"].toArray();
        out.tags.reserve(tags.size());
        for (const QJsonValue t : tags)
            out.tags << normalizeTag(t.toString());
        e.images << out;
    }

    e.lora = loraFromJson(obj["lora"]);

    if (!validEntry(e))
        return std::unexpected(LoadError{path, "missing uuid, title, or images"});

    return e;
}

EntryLoad readEntries(const QString& entryDir)
{
    EntryLoad out;

    const QDir base(entryDir);
    if (!base.exists()) {
        out.errors << LoadError{entryDir, "directory does not exist"};
        return out;
    }

    const QStringList folders = base.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    out.entries.reserve(folders.size());

    for (const QString& name : folders) {
        std::expected<Entry, LoadError> r = readEntry(base.filePath(name));
        if (r)
            out.entries << std::move(*r);
        else
            out.errors << std::move(r.error());
    }
    return out;
}

std::expected<void, LoadError> writeEntry(const Entry& e, const QString& entryFolder)
{
    const QString path = entryFolder + u'/' + kEntryFile;

    if (!validEntry(e))
        return std::unexpected(LoadError{path, "entry has no uuid, title, or images"});

    if (!QDir().mkpath(entryFolder))
        return std::unexpected(LoadError{path, "cannot create " + entryFolder});

    QJsonArray images;
    for (const EntryImage& img : e.images) {
        QJsonArray tags;
        for (const QString& t : img.tags)
            tags.append(normalizeTag(t));

        QJsonObject io;
        io["file"] = img.fileName;
        io["tags"] = tags;
        images.append(io);
    }

    QJsonObject root;
    root["v"] = kFormatVersion;
    root["uuid"] = e.uuid;
    root["title"] = e.title;
    root["comment"] = e.comment;
    root["creation"] = e.created;
    root["images"] = images;
    root["lora"] = e.lora ? QJsonValue(loraToJson(*e.lora)) : QJsonValue(QJsonValue::Null);

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return std::unexpected(LoadError{path, "cannot open for writing: " + f.errorString()});

    f.write(QJsonDocument(root).toJson());
    if (!f.commit()) return std::unexpected(LoadError{path, "write failed: " + f.errorString()});

    return {};
}

} // namespace tc
