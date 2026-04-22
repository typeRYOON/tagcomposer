#include <io/entryio.h>
#include <utils/stringutils.h>
#include <core/entry.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>

using namespace core;
using namespace utils;

namespace io {

    QList<Entry> EntryIO::loadAll(
        TagIndex& tagIndex,
        const QString& basePath,
        const EntryType entryType)
    {
        static int32_t id{ 0 };
        QList<Entry> entries;
        QDir base{ basePath };

        for (const QString& dir : base.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        {
            QFile f{ base.filePath(dir + "/__entry.json") };
            if (!f.open(QIODevice::ReadOnly)) {
                continue;
            }
            const QJsonObject obj{ QJsonDocument::fromJson(f.readAll()).object() };
            if (!obj.contains("uuid")
                || !obj.contains("title")
                || !obj.contains("images")
                || !obj.contains("creation")) {
                continue;
            }

            Entry e;
            e.uuid         = obj["uuid"].toString();
            e.title        = obj["title"].toString();
            e.creationTime = obj["creation"].toInteger();
            e.type         = entryType;
            e.id           = id++;

            const QJsonArray imagesArr = obj["images"].toArray();
            for (const QJsonValueConstRef& v : imagesArr)
            {
                const QJsonObject imgObj = v.toObject();
                if (!imgObj.contains("file") || !imgObj.contains("tags")) {
                    continue;
                }

                ImageData imgData;
                imgData.fileName = imgObj["file"].toString();
                for (const QJsonValueConstRef& t : imgObj["tags"].toArray())
                {
                    imgData.tagIds << tagIndex.getOrCreate(
                        normalizeTagInput(t.toString())
                    );
                }
                e.images << imgData;
            }
            if (validEntry(e)) {
                entries << e;
            }
        }

        return entries;
    }

    void EntryIO::save(const Entry& e)
    {

    }
}