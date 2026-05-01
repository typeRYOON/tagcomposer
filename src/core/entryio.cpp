#include <core/entryio.h>
#include <utils/stringutils.h>
#include <utils/appconfig.h>
#include <core/entry.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QFile>

using namespace utils;

namespace core {

    std::optional<Entry> EntryIO::loadOne(
        const QString& entryFolder,
        TagIndex& tagIndex)
    {
        QFile f{ entryFolder + "/__entry.json" };
        if (!f.open(QIODevice::ReadOnly)) return std::nullopt;

        const QJsonObject obj{ QJsonDocument::fromJson(f.readAll()).object() };
        if (!obj.contains("uuid")
            || !obj.contains("title")
            || !obj.contains("images")
            || !obj.contains("creation")) {
            return std::nullopt;
        }

        Entry e;
        e.uuid         = obj["uuid"].toString();
        e.title        = obj["title"].toString();
        e.comment      = obj["comment"].toString();
        e.creationTime = obj["creation"].toInteger();
        // id left default — caller assigns

        if (obj.contains("lora") && obj["lora"].isObject()) {
            const QJsonObject lo = obj["lora"].toObject();
            LoraConfig lc;
            lc.file     = lo["file"].toString();
            lc.modelStr = lo["modelStr"].toDouble(0.9);
            lc.clipStr  = lo["clipStr"].toDouble(2.0);
            lc.sha256   = lo["sha256"].toString();
            e.lora = lc;
        }

        const QJsonArray imagesArr = obj["images"].toArray();
        for (const QJsonValueConstRef& v : imagesArr) {
            const QJsonObject imgObj = v.toObject();
            if (!imgObj.contains("file") || !imgObj.contains("tags")) continue;

            ImageData imgData;
            imgData.fileName = imgObj["file"].toString();
            for (const QJsonValueConstRef& t : imgObj["tags"].toArray()) {
                imgData.tagIds << tagIndex.getOrCreate(
                    normalizeTagInput(t.toString()));
            }
            e.images << imgData;
        }

        if (!validEntry(e)) return std::nullopt;
        return e;
    }

    QList<Entry> EntryIO::loadAll(
        TagIndex& tagIndex,
        const QString& basePath)
    {
        static int32_t id{ 0 };
        QList<Entry> entries;
        QDir base{ basePath };

        for (const QString& dir : base.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            auto e = loadOne(base.filePath(dir), tagIndex);
            if (!e) continue;
            e->id = id++;
            entries << *e;
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
        root["uuid"]     = e.uuid;
        root["title"]    = e.title;
        root["comment"]  = e.comment;
        root["creation"] = (qint64)e.creationTime;
        root["images"]   = imagesArr;

        if (e.lora.has_value()) {
            QJsonObject lo;
            lo["file"]     = e.lora->file;
            lo["modelStr"] = e.lora->modelStr;
            lo["clipStr"]  = e.lora->clipStr;
            lo["sha256"]   = e.lora->sha256;
            root["lora"]   = lo;
        } else {
            root["lora"] = QJsonValue::Null;
        }

        QFile f(entryDir + "/__entry.json");
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(QJsonDocument(root).toJson());
    }
}
