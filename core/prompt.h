#pragma once
#include <core/pipeline_types.h>
#include <core/tag_groups.h>
#include <QList>
#include <QString>

namespace tc {

// A per-facet wrap applied just before assembly, so model-specific syntax
// (Anima's "@asanagi" for rStyle tags, say) lives in settings.
struct FacetFormat {
    QString facet;
    QString prefix;
    QString suffix;

    bool operator==(const FacetFormat&) const = default;
};

// group is empty for the Uncategorized bucket, which always sorts last.
struct TagBucket {
    QString group;
    QList<PipelineTag> tags;

    bool operator==(const TagBucket&) const = default;
};

// Buckets in groups.fct order. Deactivated tags stay with their peers.
QList<TagBucket> bucketByGroup(const QList<PipelineTag>& tags, const TagGroups& groups);

// Comma-joined, in bucket order, so group order is prompt order. Only tags
// reachesOutput accepts are emitted. A weight other than 1.0 wraps the tag as
// (tag:1.5), outside any facet format.
QString buildPromptString(const QList<TagBucket>& buckets, bool forJson,
                          const QList<FacetFormat>& formats = {});

} // namespace tc
