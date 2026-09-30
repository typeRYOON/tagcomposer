#pragma once
#include <core/pipeline_types.h>
#include <core/rule.h>
#include <core/tag_facets.h>
#include <QList>

namespace tc {

// Rules run in order and only see tags still marked Include; force fires a
// rule with no match. Injection skips tags already reaching the output.
// The first input.size() results map 1:1 to input; the rest are injected.
QList<PipelineTag> applyRules(const QList<Rule>& rules, const QList<PipelineTag>& input,
                              const TagFacets& defs);

bool tagMatches(const PipelineTag& tag, const RuleMatch& match);

} // namespace tc
