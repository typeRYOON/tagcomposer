#include <core/entryio.h>
#include <utils/stringutils.h>
#include <utils/appconfig.h>
#include <core/entry.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QFile>
#include <QSemaphore>
#include <QThreadPool>
#include <atomic>
#include <vector>

using namespace utils;

namespace core {

namespace {

// Entry with its tags still as strings. Splitting parse from interning lets
// loadAll parse files on worker threads (TagIndex is not thread safe).
struct ParsedEntry {
    Entry entry; // images present, tagIds empty
    QList<QList<QString>> imageTags;
    bool ok = false;
};

ParsedEntry parseEntryFile(const QString& entryFolder)
{
    ParsedEntry out;

    QFile f{entryFolder + "/__entry.json"};
    if (!f.open(QIODevice::ReadOnly)) return out;

    const QJsonObject obj{QJsonDocument::fromJson(f.readAll()).object()};
    if (!obj.contains("uuid") || !obj.contains("title") || !obj.contains("images") ||
        !obj.contains("creation")) {
        return out;
    }

    Entry& e = out.entry;
    e.uuid = obj["uuid"].toString();
    e.title = obj["title"].toString();
    e.comment = obj["comment"].toString();
    e.creationTime = obj["creation"].toInteger();
    // id left default - caller assigns

    if (obj.contains("lora") && obj["lora"].isObject()) {
        const QJsonObject lo = obj["lora"].toObject();
        LoraConfig lc;
        lc.rootKey = lo["rootKey"].toString();
        lc.file = lo["file"].toString();
        lc.modelStr = lo["modelStr"].toDouble(0.9);
        lc.clipStr = lo["clipStr"].toDouble(2.0);
        lc.sha256 = lo["sha256"].toString();
        e.lora = lc;
    }

    const QJsonArray imagesArr = obj["images"].toArray();
    for (const QJsonValueConstRef& v : imagesArr) {
        const QJsonObject imgObj = v.toObject();
        if (!imgObj.contains("file") || !imgObj.contains("tags")) continue;

        ImageData imgData;
        imgData.fileName = imgObj["file"].toString();
        QList<QString> tags;
        for (const QJsonValueConstRef& t : imgObj["tags"].toArray())
            tags << normalizeTagInput(t.toString());
        e.images << imgData;
        out.imageTags << tags;
    }

    out.ok = validEntry(e);
    return out;
}

void internTags(ParsedEntry& parsed, TagIndex& tagIndex)
{
    for (int i = 0; i < parsed.entry.images.size(); ++i)
        for (const QString& tag : parsed.imageTags[i])
            parsed.entry.images[i].tagIds << tagIndex.getOrCreate(tag);
}

} // namespace

std::optional<Entry> EntryIO::loadOne(const QString& entryFolder, TagIndex& tagIndex)
{
    ParsedEntry parsed = parseEntryFile(entryFolder);
    if (!parsed.ok) return std::nullopt;
    internTags(parsed, tagIndex);
    return parsed.entry;
}

QList<Entry> EntryIO::loadAll(TagIndex& tagIndex, const QString& basePath)
{
    QDir base{basePath};
    const QStringList dirs = base.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    // Resolve paths up front: QDir caches lazily and isn't safe to share
    // across the worker threads below.
    QStringList paths;
    paths.reserve(dirs.size());
    for (const QString& dir : dirs)
        paths << base.filePath(dir);

    // Startup cost here is thousands of small file opens, so parse in parallel
    // and intern serially (TagIndex isn't thread safe). Results stay indexed by
    // input position, so runtime entry ids match the single-threaded load.
    //
    // Hand-rolled over QThreadPool rather than QtConcurrent::blockingMapped:
    // the map machinery is a real import from Qt6Concurrent.dll, which would
    // add a DLL to the deployment (QtConcurrent::run, used elsewhere here, is
    // header-only and does not).
    const int count = int(paths.size());
    std::vector<ParsedEntry> parsed(size_t(count > 0 ? count : 0));
    std::atomic<int> next{0};

    auto drain = [&]() {
        for (int i = next.fetch_add(1); i < count; i = next.fetch_add(1))
            parsed[size_t(i)] = parseEntryFile(paths[i]);
    };

    QThreadPool* pool = QThreadPool::globalInstance();
    QSemaphore finished;
    int started = 0;
    // tryStart (not start) so a busy pool can't leave us waiting on a task
    // that never began; this thread drains the queue either way.
    for (int w = 0, workers = qMin(pool->maxThreadCount(), count); w < workers; ++w) {
        if (pool->tryStart([&drain, &finished]() {
                drain();
                finished.release();
            })) {
            ++started;
        }
    }
    drain();
    finished.acquire(started);

    QList<Entry> entries;
    entries.reserve(count);
    int32_t id{0};

    for (ParsedEntry& p : parsed) {
        if (!p.ok) continue;
        internTags(p, tagIndex);
        p.entry.id = id++;
        entries << std::move(p.entry);
    }

    return entries;
}

void EntryIO::save(const Entry& e, const TagIndex& tagIndex)
{
    const QString entryDir = utils::BASE_PATH + "/data/entry/" + e.uuid;

    if (e.images.isEmpty()) {
        QDir(entryDir).removeRecursively();
        return;
    }

    QDir().mkpath(entryDir);

    QJsonArray imagesArr;
    for (const ImageData& img : e.images) {
        QJsonArray tagsArr;
        for (int32_t tagId : img.tagIds)
            tagsArr.append(tagIndex.getTag(tagId));

        QJsonObject imgObj;
        imgObj["file"] = img.fileName;
        imgObj["tags"] = tagsArr;
        imagesArr.append(imgObj);
    }

    QJsonObject root;
    root["uuid"] = e.uuid;
    root["title"] = e.title;
    root["comment"] = e.comment;
    root["creation"] = (qint64)e.creationTime;
    root["images"] = imagesArr;

    if (e.lora.has_value()) {
        QJsonObject lo;
        lo["rootKey"] = e.lora->rootKey;
        lo["file"] = e.lora->file;
        lo["modelStr"] = e.lora->modelStr;
        lo["clipStr"] = e.lora->clipStr;
        lo["sha256"] = e.lora->sha256;
        root["lora"] = lo;
    }
    else {
        root["lora"] = QJsonValue::Null;
    }

    QFile f(entryDir + "/__entry.json");
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument(root).toJson());
}
} // namespace core
