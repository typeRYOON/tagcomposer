#pragma once
#include <QString>
#include <QList>

namespace core {

class FacetIndex;

// ── Match ─────────────────────────────────────────────────────────────────────

enum class MatchType {
    AnyTagFacets,  // tag carries ALL listed facets
    AnyTagName,    // tag name matches a glob pattern
};

// One atomic condition, optionally negated.
struct MatchClause {
    bool           negate   { false };
    MatchType      type     { MatchType::AnyTagFacets };
    QList<QString> facets;   // AnyTagFacets: ALL must be present
    QString        nameGlob; // AnyTagName
};

// OR of AND-groups: a tag matches if ANY group has ALL its clauses satisfied.
//
// Examples (file syntax):
//   anyTag(facets: hairstyle) AND NOT anyTag(facets: bangs)
//   anyTag(facets: eye_color) OR anyTag(facets: eye_shape)
//   anyTag(facets: clothing) OR anyTag(name: "*dress*")
struct RuleMatch {
    QList<QList<MatchClause>> orGroups;
};

// ── Action ────────────────────────────────────────────────────────────────────

enum class ActionType {
    Skip,     // remove matched tag(s) from the output
    Add,      // inject a new tag without removing anything
    Replace,  // remove matched tag(s) and inject a new one
    Flag,     // keep matched tag(s) but mark them with a label
    Delete,   // permanently remove matched tag(s) from the composer's active set
};

struct RuleAction {
    ActionType     type{ ActionType::Skip };
    QList<QString> arguments; // new tags for Add/Replace; single label for Flag
};

// ── Rule ──────────────────────────────────────────────────────────────────────

struct Rule {
    QString    name;
    bool       enabled { true };
    bool       force   { false }; // fire action even when no tags matched
    RuleMatch  match;
    RuleAction action;
};

// ── Pipeline types (produced by evaluation) ───────────────────────────────────

enum class RuleResult {
    Include,     // kept as-is
    Skipped,     // removed by a Skip rule
    Replaced,    // removed by a Replace rule (paired with an Injected tag)
    Injected,    // added by an Add or Replace rule
    Flagged,     // kept but marked by a Flag rule
    NoFacets,    // no definition in FacetIndex — passes through unaffected by rules
    Deactivated, // user-muted: excluded from pipeline and rules, displayed separately
    Deleted,     // matched by a Delete rule — composer removes from active set
};

struct PipelineTag {
    QString        tag;
    QString        sourceTag;  // original tag before variable expansion (empty if none)
    QList<QString> facets;
    RuleResult     result     { RuleResult::Include };
    QString        ruleSource; // name of the rule that set this result
    QString        flagLabel;  // populated when result == Flagged
    float          weight     { 1.0f }; // prompt attention weight; 1.0 = no wrapper
};

struct CategoryGroup {
    QString            category; // empty string = "Uncategorized"
    QList<PipelineTag> tags;
};

// ── RuleEngine ────────────────────────────────────────────────────────────────

class RuleEngine {
public:
    static RuleEngine loadFromFile(const QString& path, QStringList* errors = nullptr);
    void saveToFile(const QString& path) const;

    QList<Rule>&       rules();
    const QList<Rule>& rules() const;

    // Applies all enabled rules to the input set. Rules run in order;
    // only tags currently marked Include are tested by each rule.
    QList<PipelineTag> evaluate(
        const QList<PipelineTag>& input,
        const FacetIndex&         facets
    ) const;

private:
    QList<Rule> m_rules;

    static bool tagMatchesRule(const PipelineTag& pt, const RuleMatch& match);
    static bool clauseMatches(const PipelineTag& pt, const MatchClause& clause);
    static bool globMatch(const QString& pattern, const QString& text);
};

} // namespace core
