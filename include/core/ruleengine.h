#pragma once
#include <QString>
#include <QList>

namespace core {

class FacetIndex;

// ── Match ─────────────────────────────────────────────────────────────────────

enum class MatchType {
    AnyTagFacets, // tag carries ALL listed facets
    AnyTagName,   // tag name matches a glob pattern
};

struct MatchClause {
    bool negate{false};
    MatchType type{MatchType::AnyTagFacets};
    QList<QString> facets;
    QString nameGlob;
};

// OR of AND-groups: a tag matches if ANY group has ALL its clauses satisfied.
struct RuleMatch {
    QList<QList<MatchClause>> orGroups;
};

// ── Action ────────────────────────────────────────────────────────────────────

enum class ActionType {
    Skip,    // remove matched tag from output
    Add,     // inject a new tag without removing anything
    Replace, // remove matched, inject a new one
    Flag,    // keep matched, attach a label
    Delete,  // permanently remove from the composer's active set
};

struct RuleAction {
    ActionType type{ActionType::Skip};
    QList<QString> arguments; // new tags for Add/Replace, label for Flag
};

// ── Rule ──────────────────────────────────────────────────────────────────────

struct Rule {
    QString name;
    bool enabled{true};
    bool force{false}; // fire action even when no tags matched
    RuleMatch match;
    RuleAction action;
};

// ── Pipeline types ────────────────────────────────────────────────────────────

enum class RuleResult {
    Include,
    Skipped,
    Replaced,    // paired with an Injected entry that contains the new tag
    Injected,    // added by Add or Replace
    Flagged,
    NoFacets,    // no FacetIndex definition; bypasses rules
    Deactivated, // user-muted; displayed separately
    Deleted,     // composer removes from the active set
};

struct PipelineTag {
    QString tag;
    QString sourceTag; // pre-variable-expansion form, empty if none
    QList<QString> facets;
    RuleResult result{RuleResult::Include};
    QString ruleSource;
    QString flagLabel;
    float weight{1.0f};
};

struct CategoryGroup {
    QString category; // empty = Uncategorized
    QList<PipelineTag> tags;
};

// ── RuleEngine ────────────────────────────────────────────────────────────────

class RuleEngine {
public:
    static RuleEngine loadFromFile(const QString& path, QStringList* errors = nullptr);
    void saveToFile(const QString& path) const;

    QList<Rule>& rules();
    const QList<Rule>& rules() const;

    // Rules run in order; only Include tags are eligible per rule.
    QList<PipelineTag> evaluate(const QList<PipelineTag>& input, const FacetIndex& facets) const;

private:
    QList<Rule> m_rules;

    static bool tagMatchesRule(const PipelineTag& pt, const RuleMatch& match);
    static bool clauseMatches(const PipelineTag& pt, const MatchClause& clause);
    static bool globMatch(const QString& pattern, const QString& text);
};

} // namespace core
