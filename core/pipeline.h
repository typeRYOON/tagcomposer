#pragma once
#include <core/composer_doc.h>
#include <core/pipeline_types.h>
#include <core/rule.h>
#include <core/tag_facets.h>
#include <core/variables.h>
#include <QList>

namespace tc {

// What the pipeline needs beyond the document. Everything is borrowed; the
// caller owns it. vars may be null, in which case no expansion happens.
struct PipelineContext {
    const TagFacets* defs = nullptr;
    const QList<Rule>* rules = nullptr;
    const Variables* vars = nullptr;
};

// Resolves each active tag to its final text and facets, then runs the rules.
// Returns a flat list in activeTags order, with rule-injected tags appended.
// Bucketing into display groups is a separate step.
//
// Per tag: variables are expanded (pre-expansion text kept in sourceTag),
// facets come from customFacets, else the definition of the expanded tag, else
// the definition of the tag with its variables stripped. A tag with no facets
// is marked NoFacets and bypasses the rules; a deactivated one is marked
// Deactivated and does the same.
//
// Two active tags that expand to the same text collapse into one. The survivor
// keeps the merged weight -- explicit over default, heavier over lighter -- so
// the outcome does not depend on their order in the list.
QList<PipelineTag> evaluate(const ComposerDoc& doc, const PipelineContext& ctx);

} // namespace tc
