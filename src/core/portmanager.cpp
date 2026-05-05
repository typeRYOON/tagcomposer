#include <core/portmanager.h>
#include <core/entry.h>
#include <core/entryio.h>
#include <utils/appconfig.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>

namespace core {

namespace {

// Recursive copy that overwrites destination files (re-imports clobber).
bool copyDirContents(const QDir& src, const QDir& dst)
{
    if (!QDir().mkpath(dst.absolutePath())) return false;

    for (const QFileInfo& fi : src.entryInfoList(QDir::Files)) {
        const QString out = dst.absoluteFilePath(fi.fileName());
        if (QFile::exists(out)) QFile::remove(out);
        if (!QFile::copy(fi.absoluteFilePath(), out)) return false;
    }
    for (const QFileInfo& fi : src.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!copyDirContents(QDir(fi.absoluteFilePath()),
                             QDir(dst.absoluteFilePath(fi.fileName()))))
            return false;
    }
    return true;
}

} // namespace

// ---- Export

bool PortManager::exportEntries(const QString& query, const QString& destFolder, EntryModel* model,
                                const FacetIndex& facets, QStringList* errors)
{
    auto fail = [&](const QString& msg) -> bool {
        if (errors) *errors << msg;
        return false;
    };

    if (!model) return fail("EntryModel is null");

    QDir destDir(destFolder);
    if (!destDir.mkpath(".")) return fail("Failed to create destination folder");

    const QList<Entry*> matched = model->filter(query);
    if (matched.isEmpty()) return fail("Query matched 0 entries");

    QDir entriesDir(destDir.absoluteFilePath("entries"));
    if (!QDir().mkpath(entriesDir.absolutePath())) return fail("Failed to create entries folder");

    QSet<QString> usedTags;

    for (Entry* e : matched) {
        const QString srcEntry = utils::BASE_PATH + "/data/entry/" + e->uuid;
        const QString dstEntry = entriesDir.absoluteFilePath(e->uuid);

        if (!copyDirContents(QDir(srcEntry), QDir(dstEntry))) {
            if (errors) *errors << QString("Failed to copy entry %1").arg(e->title);
            continue;
        }

        for (const ImageData& img : e->images) {
            for (int32_t tagId : img.tagIds) {
                const QString tagName = model->tagIndex().getTag(tagId);
                if (!tagName.isEmpty()) usedTags.insert(tagName);
            }
        }
    }

    // Subset tag_definitions.fct: only tags used by exported entries.
    const QString defsPath = destDir.absoluteFilePath("tag_definitions.fct");
    QFile defsFile(defsPath);
    if (!defsFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return fail("Failed to write tag_definitions.fct");

    QTextStream ts(&defsFile);
    QStringList sortedTags(usedTags.begin(), usedTags.end());
    sortedTags.sort();
    for (const QString& tag : sortedTags) {
        if (!facets.hasFacets(tag)) continue;
        const QList<QString> tagFacets = facets.facetsFor(tag);
        ts << tag << '=' << tagFacets.join(',') << '\n';
    }

    return true;
}

// ---- Scan

PortScan PortManager::scanImport(const QString& srcFolder, EntryModel* model,
                                 const FacetIndex& facets)
{
    PortScan scan;
    if (!model) return scan;

    QDir entriesDir(srcFolder + "/entries");
    if (entriesDir.exists()) {
        for (const QString& sub : entriesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString folder = entriesDir.absoluteFilePath(sub);
            QFile json(folder + "/__entry.json");
            if (!json.open(QIODevice::ReadOnly)) continue;

            const QJsonObject obj = QJsonDocument::fromJson(json.readAll()).object();
            const QString uuid = obj["uuid"].toString();
            if (uuid.isEmpty()) continue;

            PortEntryRef ref;
            ref.uuid = uuid;
            ref.sourceFolder = folder;
            ref.title = obj["title"].toString();
            ref.duplicate = (model->entryByUuid(uuid) != nullptr);
            scan.entries << ref;
        }
    }

    QSet<QString> facetSet;
    QFile defsFile(srcFolder + "/tag_definitions.fct");
    if (defsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        for (const QString& raw : QString::fromUtf8(defsFile.readAll()).split('\n')) {
            const QString line = raw.trimmed();
            if (line.isEmpty() || line.startsWith('#') || !line.contains('=')) continue;

            const int eq = line.indexOf('=');
            const QString tag = line.left(eq).trimmed();
            if (tag.isEmpty()) continue;

            QList<QString> tagFacets;
            for (const QString& part : line.mid(eq + 1).split(',')) {
                const QString t = part.trimmed();
                if (!t.isEmpty()) {
                    tagFacets << t;
                    facetSet.insert(t);
                }
            }

            PortTagDef def;
            def.tag = tag;
            def.facets = tagFacets;
            def.collision = facets.hasFacets(tag);
            scan.tagDefs << def;
        }
    }

    QList<QString> sortedFacets(facetSet.begin(), facetSet.end());
    sortedFacets.sort();
    scan.sourceFacets = sortedFacets;

    return scan;
}

// ---- Apply

PortResult PortManager::applyImport(const PortScan& scan, const PortConfig& config,
                                    EntryModel* model, FacetIndex& facets,
                                    const QString& dataEntryDir, const QString& tagDefinitionsPath)
{
    PortResult result;
    if (!model) {
        result.errors << "EntryModel is null";
        return result;
    }

    // .bak captures the on-disk state before any mutation; rollback target.
    if (QFile::exists(tagDefinitionsPath)) {
        const QString bakPath = tagDefinitionsPath + ".bak";
        if (QFile::exists(bakPath)) QFile::remove(bakPath);
        if (!QFile::copy(tagDefinitionsPath, bakPath))
            result.errors << "Could not write .bak (proceeding anyway)";
    }

    QDir().mkpath(dataEntryDir);
    for (const PortEntryRef& ref : scan.entries) {
        if (ref.duplicate) {
            ++result.entriesSkipped;
            continue;
        }

        const QString dst = dataEntryDir + "/" + ref.uuid;
        if (!copyDirContents(QDir(ref.sourceFolder), QDir(dst))) {
            result.errors << QString("Failed to copy entry: %1").arg(ref.title);
            continue;
        }

        if (auto entry = EntryIO::loadOne(dst, model->tagIndex())) {
            model->addEntry(*entry);
            ++result.entriesImported;
        }
        else {
            result.errors << QString("Failed to parse entry after copy: %1").arg(ref.title);
        }
    }

    for (const PortTagDef& def : scan.tagDefs) {
        // Apply facetMapping: missing key or empty value means drop.
        QList<QString> mapped;
        QSet<QString> seen;
        for (const QString& f : def.facets) {
            const auto it = config.facetMapping.find(f);
            if (it == config.facetMapping.end()) continue;
            const QString target = it.value();
            if (target.isEmpty()) continue;
            if (seen.contains(target)) continue;
            seen.insert(target);
            mapped << target;
        }

        if (mapped.isEmpty()) {
            ++result.tagsDroppedEmpty;
            continue;
        }

        if (def.collision) {
            switch (config.tagConflict) {
            case TagConflictMode::Skip:
                ++result.tagsSkipped;
                break;
            case TagConflictMode::Merge: {
                QList<QString> merged = facets.facetsFor(def.tag);
                QSet<QString> mset(merged.begin(), merged.end());
                for (const QString& f : mapped) {
                    if (!mset.contains(f)) {
                        merged << f;
                        mset.insert(f);
                    }
                }
                facets.setDefinition(def.tag, merged);
                ++result.tagsMerged;
                break;
            }
            case TagConflictMode::Overwrite:
                facets.setDefinition(def.tag, mapped);
                ++result.tagsOverwritten;
                break;
            }
        }
        else {
            facets.setDefinition(def.tag, mapped);
            ++result.tagsAdded;
        }
    }

    facets.saveDefinitions(tagDefinitionsPath);
    return result;
}

} // namespace core
