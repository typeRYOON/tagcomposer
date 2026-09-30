#include <core/rule_eval.h>
#include <QHash>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

using GlobCache = QHash<QString, QRegularExpression>;

const QRegularExpression& globFor(GlobCache& cache, const QString& pattern)
{
    auto it = cache.find(pattern);
    if (it == cache.end()) {
        const QString re = u"\\A"_s
            + QRegularExpression::escape(pattern).replace(u"\\*"_s, u".*"_s).replace(u"\\?"_s, u"."_s)
            + u"\\z"_s;
        it = cache.insert(pattern, QRegularExpression(re, QRegularExpression::CaseInsensitiveOption));
    }
    return it.value();
}

bool clauseMatches(const PipelineTag& pt, const MatchClause& clause, GlobCache& cache)
{
    switch (clause.type) {
    case MatchType::AnyTagFacets:
        if (clause.facets.isEmpty()) return false;
        for (const QString& f : clause.facets)
            if (!pt.facets.contains(f)) return false;
        return true;

    case MatchType::AnyTagName:
        return globFor(cache, clause.nameGlob).match(pt.tag).hasMatch();
    }
    return false;
}

bool matchesWith(const PipelineTag& pt, const RuleMatch& match, GlobCache& cache)
{
    if (match.orGroups.isEmpty()) return false;

    for (const QList<MatchClause>& group : match.orGroups) {
        bool groupOk = true;
        for (const MatchClause& clause : group) {
            bool ok = clauseMatches(pt, clause, cache);
            if (clause.negate) ok = !ok;
            if (!ok) {
                groupOk = false;
                break;
            }
        }
        if (groupOk) return true;
    }
    return false;
}

bool alreadyPresent(const QList<PipelineTag>& tags, const QString& tag)
{
    for (const PipelineTag& pt : tags)
        if (reachesOutput(pt.result) && pt.tag == tag) return true;
    return false;
}

PipelineTag inject(const QString& tag, const QString& ruleName, const TagFacets& defs)
{
    PipelineTag pt;
    pt.tag = tag;
    pt.facets = defs.facetsFor(tag);
    pt.result = TagResult::Injected;
    pt.ruleSource = ruleName;
    return pt;
}

} // namespace

bool tagMatches(const PipelineTag& tag, const RuleMatch& match)
{
    GlobCache cache;
    return matchesWith(tag, match, cache);
}

QList<PipelineTag> applyRules(const QList<Rule>& rules, const QList<PipelineTag>& input,
                              const TagFacets& defs)
{
    QList<PipelineTag> working = input;
    GlobCache cache;

    for (const Rule& rule : rules) {
        if (!rule.enabled) continue;

        QList<qsizetype> matched;
        for (qsizetype i = 0; i < working.size(); ++i)
            if (working[i].result == TagResult::Include && matchesWith(working[i], rule.match, cache))
                matched << i;

        if (matched.isEmpty() && !rule.force) continue;

        switch (rule.action.type) {
        case ActionType::Skip:
            for (qsizetype i : matched) {
                working[i].result = TagResult::Skipped;
                working[i].ruleSource = rule.name;
            }
            break;

        case ActionType::Delete:
            for (qsizetype i : matched) {
                working[i].result = TagResult::Deleted;
                working[i].ruleSource = rule.name;
            }
            break;

        case ActionType::Flag:
            for (qsizetype i : matched) {
                working[i].result = TagResult::Flagged;
                working[i].flagLabel = rule.action.arguments.value(0);
                working[i].ruleSource = rule.name;
            }
            break;

        case ActionType::Add:
            for (const QString& tag : rule.action.arguments)
                if (!alreadyPresent(working, tag)) working << inject(tag, rule.name, defs);
            break;

        case ActionType::Replace:
            for (qsizetype i : matched) {
                working[i].result = TagResult::Replaced;
                working[i].ruleSource = rule.name;
            }
            for (const QString& tag : rule.action.arguments)
                if (!alreadyPresent(working, tag)) working << inject(tag, rule.name, defs);
            break;
        }
    }

    return working;
}

} // namespace tc
