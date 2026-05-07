#pragma once
#include <core/entrymodel.h>
#include <core/facetindex.h>
#include <QString>
#include <QStringList>
#include <QList>
#include <QHash>

namespace core {

class DanbooruIndex;

struct PortEntryRef {
    QString uuid;
    QString sourceFolder;
    QString title;
    bool duplicate = false; // EntryModel already has this uuid
};

struct PortTagDef {
    QString tag;
    QList<QString> facets;  // raw, pre-mapping
    bool collision = false; // FacetIndex already has a definition for tag
};

// Snapshot collected before any disk mutation, so the dialog can render
// a mapping table and summary first.
struct PortScan {
    QList<PortEntryRef> entries;
    QList<PortTagDef> tagDefs;
    QList<QString> sourceFacets; // sorted union from tagDefs
};

enum class TagConflictMode { Skip, Merge, Overwrite };

struct PortConfig {
    // sourceFacet -> destFacet; empty value or missing key drops the facet.
    QHash<QString, QString> facetMapping;
    TagConflictMode tagConflict = TagConflictMode::Skip;
};

struct PortResult {
    int entriesImported = 0;
    int entriesSkipped = 0; // duplicates
    int tagsAdded = 0;
    int tagsSkipped = 0;    // collision + Skip mode
    int tagsMerged = 0;
    int tagsOverwritten = 0;
    int tagsDroppedEmpty = 0; // all facets dropped after mapping
    QStringList errors;
};

class PortManager {
public:
    // Copies matching entry folders into <destFolder>/entries/<uuid> and
    // writes a tag_definitions.fct subset for only the used tags.
    // If includeUnusedDanbooruDefs is true, also writes definitions for tags
    // that are defined in `facets` but unused by exported entries, provided
    // they appear in `danbooru` (filters out typos / orphaned defs).
    static bool exportEntries(const QString& query, const QString& destFolder, EntryModel* model,
                              const FacetIndex& facets, QStringList* errors = nullptr,
                              bool includeUnusedDanbooruDefs = false,
                              const DanbooruIndex* danbooru = nullptr);

    // Pure inspection - no disk mutation. Marks duplicates / collisions.
    static PortScan scanImport(const QString& srcFolder, EntryModel* model,
                               const FacetIndex& facets);

    // Backs up tag_definitions.fct to .bak first, then copies entries,
    // applies facet mapping, resolves collisions, and persists.
    static PortResult applyImport(const PortScan& scan, const PortConfig& config, EntryModel* model,
                                  FacetIndex& facets, const QString& dataEntryDir,
                                  const QString& tagDefinitionsPath);
};

} // namespace core
