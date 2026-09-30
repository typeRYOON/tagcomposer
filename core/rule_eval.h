#pragma once
#include <core/pipeline_types.h>
#include <core/rule.h>
#include <core/tag_facets.h>
#include <QList>

namespace tc {

// Rules run in list order, and each only sees tags still marked Include, so an
// earlier rule's verdict is final for later ones. A rule with force fires even
// when nothing matched, which is how Replace injects a tag unconditionally.
// A tag is injected only when the list does not already carry it in a result
// that reaches the prompt, so one tag means one row and one weight. A tag that
// was just skipped, deleted or replaced does not count as present, which is how
// a later rule reinstates one.
// The first input.size() results correspond one to one with input, in order;
// anything past that is an injected tag. Callers rely on this to merge the
// result back into a larger list.
QList<PipelineTag> applyRules(const QList<Rule>& rules, const QList<PipelineTag>& input,
                              const TagFacets& defs);

// Exposed for a rule editor that wants to preview what an expression selects.
bool tagMatches(const PipelineTag& tag, const RuleMatch& match);

} // namespace tc
