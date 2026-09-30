#include <core/pipeline.h>
#include <core/rule_eval.h>
#include <QHash>

namespace tc {
namespace {

QStringList facetsForTag(const ComposerDoc& doc, const PipelineContext& ctx,
                         const QString& activeTag, const PipelineTag& pt)
{
    const auto custom = doc.customFacets.constFind(activeTag);
    if (custom != doc.customFacets.constEnd() && !custom->isEmpty()) return *custom;

    if (!ctx.defs) return {};

    QStringList facets = ctx.defs->facetsFor(pt.tag);
    if (facets.isEmpty() && !pt.sourceTag.isEmpty()) {
        const QString base = stripVariables(pt.sourceTag);
        if (!base.isEmpty()) facets = ctx.defs->facetsFor(base);
    }
    return facets;
}

} // namespace

QList<PipelineTag> evaluate(const ComposerDoc& doc, const PipelineContext& ctx)
{
    QList<PipelineTag> resolved;
    QHash<QString, qsizetype> indexOf;
    QHash<QString, Weight> picked;

    for (const QString& activeTag : doc.activeTags) {
        // Deactivated tags come back as display-only rows after the rules.
        if (doc.deactivated.contains(activeTag)) continue;

        PipelineTag pt;
        pt.tag = activeTag;

        if (ctx.vars && hasVariable(activeTag)) {
            pt.sourceTag = activeTag;
            pt.tag = ctx.vars->expand(activeTag);
        }
        if (pt.tag.isEmpty()) continue;

        const Weight w = weightOf(doc, activeTag);

        const auto seen = indexOf.constFind(pt.tag);
        if (seen != indexOf.constEnd()) {
            const Weight merged = mergeWeights(picked.value(pt.tag), w);
            picked.insert(pt.tag, merged);
            resolved[*seen].weight = merged.value;
            continue;
        }

        pt.weight = w.value;
        pt.facets = facetsForTag(doc, ctx, activeTag, pt);

        if (pt.facets.isEmpty()) pt.result = TagResult::NoFacets;

        indexOf.insert(pt.tag, resolved.size());
        picked.insert(pt.tag, w);
        resolved << pt;
    }

    // Display-only rows for muted tags. No sourceTag (a row with one is styled as
    // a variable) and no facets (the page remembers where each was turned off).
    auto appendDeactivated = [&doc](QList<PipelineTag>& list) {
        for (const QString& activeTag : doc.activeTags) {
            if (!doc.deactivated.contains(activeTag)) continue;

            PipelineTag pt;
            pt.tag = activeTag;
            pt.result = TagResult::Deactivated;
            list << pt;
        }
    };

    if (!ctx.rules || !ctx.defs) {
        appendDeactivated(resolved);
        return resolved;
    }

    QList<PipelineTag> forRules;
    for (const PipelineTag& pt : resolved)
        if (pt.result == TagResult::Include) forRules << pt;

    const QList<PipelineTag> afterRules = applyRules(*ctx.rules, forRules, *ctx.defs);

    QList<PipelineTag> out;
    out.reserve(afterRules.size() + resolved.size() - forRules.size());

    qsizetype next = 0;
    for (const PipelineTag& pt : resolved) {
        if (pt.result == TagResult::Include)
            out << afterRules[next++];
        else
            out << pt;
    }
    for (qsizetype i = forRules.size(); i < afterRules.size(); ++i)
        out << afterRules[i];

    appendDeactivated(out);
    return out;
}

} // namespace tc
