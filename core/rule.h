#pragma once
#include <QList>
#include <QString>
#include <QStringList>

namespace tc {

enum class MatchType {
    AnyTagFacets,
    AnyTagName,
};

struct MatchClause {
    bool negate = false;
    MatchType type = MatchType::AnyTagFacets;
    QStringList facets; // AnyTagFacets: all required
    QString nameGlob;   // AnyTagName

    bool operator==(const MatchClause&) const = default;
};

// OR of AND-groups.
struct RuleMatch {
    QList<QList<MatchClause>> orGroups;

    bool operator==(const RuleMatch&) const = default;
};

enum class ActionType {
    Skip,    // drop the matched tag from output
    Add,     // inject new tags, matched tags untouched
    Replace, // drop matched, inject new
    Flag,    // keep matched, attach a label
    Delete,  // remove from the composer's active set
};

struct RuleAction {
    ActionType type = ActionType::Skip;
    QStringList arguments; // new tags for Add and Replace, label for Flag

    bool operator==(const RuleAction&) const = default;
};

struct Rule {
    QString uuid; // identity, survives a rename
    QString name;
    bool enabled = true;
    bool force = false; // fire even with no match
    RuleMatch match;
    RuleAction action;

    bool operator==(const Rule&) const = default;
};

} // namespace tc
