#pragma once
#include <core/pipeline_types.h>
#include <core/tag_groups.h>
#include <QList>
#include <QString>

namespace tc {

// Per-facet wrap applied at assembly, for model-specific syntax.
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

// Comma-joined in bucket order, reachesOutput tags only. A non-1.0 weight
// wraps as (tag:1.5), outside any facet format.
QString buildPromptString(const QList<TagBucket>& buckets, bool forJson,
                          const QList<FacetFormat>& formats = {});

} // namespace tc
