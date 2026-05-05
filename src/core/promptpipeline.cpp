#include <core/promptpipeline.h>
#include <utils/stringutils.h>
#include <utils/logger.h>
#include <QSet>

namespace core {

PromptPipeline::PromptPipeline(FacetIndex* facets, RuleEngine* rules, VariableIndex* vars,
                               QObject* parent)
    : QObject(parent), m_facets(facets), m_rules(rules), m_varIndex(vars)
{
}

// ---- Push

void PromptPipeline::push(const QList<QString>& tags)
{
    emit pipelineReady(evaluate(tags));
}

QList<CategoryGroup> PromptPipeline::evaluate(const QList<QString>& tags) const
{
    // 1. Resolve facets. Tags without a definition get marked NoFacets and
    //    bypass the rule engine but stay in the output.
    QList<PipelineTag> resolved;
    QList<QString> noFacetNames;
    QSet<QString> seenTags;

    for (const QString& rawTag : tags) {
        PipelineTag pt;

        if (m_varIndex && VariableIndex::hasVariable(rawTag)) {
            pt.sourceTag = rawTag;
            pt.tag = m_varIndex->expand(rawTag);
        }
        else {
            pt.tag = rawTag;
        }

        if (seenTags.contains(pt.tag)) continue;
        seenTags.insert(pt.tag);

        pt.facets = m_facets->facetsFor(pt.tag);
        if (pt.facets.isEmpty() && !pt.sourceTag.isEmpty()) {
            const QString base = VariableIndex::stripVariables(pt.sourceTag);
            if (!base.isEmpty()) pt.facets = m_facets->facetsFor(base);
        }
        if (pt.facets.isEmpty()) {
            pt.result = RuleResult::NoFacets;
            noFacetNames << pt.tag;
        }
        resolved << pt;
    }

    if (!noFacetNames.isEmpty()) {
        utils::Logger::instance().log(QString("%1 tag(s) without facet definitions: %2")
                                          .arg(noFacetNames.size())
                                          .arg(noFacetNames.join(", ")));
    }

    // 2. Run rules over Include tags only.
    QList<PipelineTag> forRules;
    for (const PipelineTag& pt : resolved)
        if (pt.result == RuleResult::Include) forRules << pt;

    const QList<PipelineTag> afterRules = m_rules->evaluate(forRules, *m_facets);

    // 3. NoFacets first (Uncategorized), then rule-engine output.
    QList<PipelineTag> final;
    for (const PipelineTag& pt : resolved)
        if (pt.result == RuleResult::NoFacets) final << pt;
    final << afterRules;

    return groupByCategory(final);
}

// ---- Grouping

QList<CategoryGroup> PromptPipeline::groupByCategory(const QList<PipelineTag>& tags) const
{
    // Group order matches the user's category definition order.
    QHash<QString, int> catIndex;
    QList<CategoryGroup> groups;

    for (const QString& cat : m_facets->allCategories()) {
        catIndex[cat] = groups.size();
        groups << CategoryGroup{cat, {}};
    }

    // Uncategorized bucket for NoFacets and untyped Injected tags.
    const int uncatIdx = groups.size();
    groups << CategoryGroup{"", {}};

    for (const PipelineTag& pt : tags) {
        QString cat;
        for (const QString& f : pt.facets) {
            cat = m_facets->categoryFor(f);
            if (!cat.isEmpty()) break;
        }

        groups[catIndex.value(cat, uncatIdx)].tags << pt;
    }

    QList<CategoryGroup> out;
    for (const CategoryGroup& g : groups)
        if (!g.tags.isEmpty()) out << g;

    return out;
}

// ---- Prompt string

QString PromptPipeline::buildPromptString(const QList<CategoryGroup>& groups, bool forJson)
{
    auto fmtWeight = [](float w) -> QString {
        QString s = QString::number(double(w), 'f', 2);
        while (s.endsWith('0'))
            s.chop(1);
        if (s.endsWith('.')) s.chop(1);
        return s;
    };

    QList<QString> parts;
    for (const CategoryGroup& g : groups) {
        QList<const PipelineTag*> active;
        for (const PipelineTag& pt : g.tags)
            if (pt.result == RuleResult::Include || pt.result == RuleResult::Injected ||
                pt.result == RuleResult::NoFacets)
                active << &pt;

        for (const PipelineTag* pt : active) {
            const QString s = utils::serializeTagForPrompt(pt->tag, forJson);
            if (qAbs(pt->weight - 1.0f) < 0.0001f)
                parts << s;
            else
                parts << "(" + s + ":" + fmtWeight(pt->weight) + ")";
        }
    }

    return parts.join(", ");
}

} // namespace core
