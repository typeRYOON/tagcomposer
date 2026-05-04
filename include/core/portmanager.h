#pragma once
#include <core/entrymodel.h>
#include <core/facetindex.h>
#include <QString>
#include <QStringList>
#include <QList>
#include <QHash>

namespace core {

// One entry sourced from an import bundle.
struct PortEntryRef {
    QString uuid;
    QString sourceFolder; // absolute path of the entry's folder in the bundle
    QString title;
    bool duplicate = false; // true if EntryModel already has this uuid
};

// One tag definition sourced from the bundle's tag_definitions.fct.
struct PortTagDef {
    QString tag;
    QList<QString> facets;  // raw facets from the bundle (pre-mapping)
    bool collision = false; // true if FacetIndex already has a definition for tag
};

// Snapshot of what an import would touch - gathered up-front so the dialog
// can render a mapping table and a summary before any disk mutation.
struct PortScan {
    QList<PortEntryRef> entries;
    QList<PortTagDef> tagDefs;
    QList<QString> sourceFacets; // sorted union of facets seen in tagDefs
};

enum class TagConflictMode { Skip, Merge, Overwrite };

struct PortConfig {
    // source facet name -> destination facet name. An empty value (or absent
    // key) means "drop this facet" - strip from any imported tag definition.
    QHash<QString, QString> facetMapping;
    TagConflictMode tagConflict = TagConflictMode::Skip;
};

struct PortResult {
    int entriesImported = 0;
    int entriesSkipped = 0; // duplicates
    int tagsAdded = 0;
    int tagsSkipped = 0; // collision + Skip mode
    int tagsMerged = 0;
    int tagsOverwritten = 0;
    int tagsDroppedEmpty = 0; // all facets stripped after mapping → omitted
    QStringList errors;
};

class PortManager {
public:
    // Resolve query via EntryModel::filter, copy each matching entry's folder
    // into <destFolder>/entries/<uuid>, and write a subset tag_definitions.fct
    // containing only facet definitions for tags actually used by the export.
    static bool exportEntries(const QString& query, const QString& destFolder, EntryModel* model,
                              const FacetIndex& facets, QStringList* errors = nullptr);

    // Read the bundle without mutating anything. Marks duplicates / collisions
    // by consulting the live model + facet index.
    static PortScan scanImport(const QString& srcFolder, EntryModel* model,
                               const FacetIndex& facets);

    // Apply the scan according to config. Backs up tag_definitions.fct to
    // {tagDefinitionsPath}.bak before any in-memory mutation, then:
    //   - copies non-duplicate entry folders into dataEntryDir/<uuid>
    //   - adds them to EntryModel via addEntry (assigns a runtime id)
    //   - rewrites each imported tag def per facetMapping, applies tagConflict
    //   - persists FacetIndex via saveDefinitions
    static PortResult applyImport(const PortScan& scan, const PortConfig& config, EntryModel* model,
                                  FacetIndex& facets, const QString& dataEntryDir,
                                  const QString& tagDefinitionsPath);
};

} // namespace core
