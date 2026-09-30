#pragma once
#include <core/composer_doc.h>
#include <core/pipeline_types.h>
#include <core/rule.h>
#include <core/tag_facets.h>
#include <core/variables.h>
#include <QList>

namespace tc {

// Borrowed. vars may be null (no expansion).
struct PipelineContext {
    const TagFacets* defs = nullptr;
    const QList<Rule>* rules = nullptr;
    const Variables* vars = nullptr;
};

// Expands variables, resolves facets (custom, then expanded tag, then tag with
// variables stripped) and runs the rules. Injected tags are appended. Tags that
// expand to the same text merge, keeping the stronger weight.
QList<PipelineTag> evaluate(const ComposerDoc& doc, const PipelineContext& ctx);

} // namespace tc
