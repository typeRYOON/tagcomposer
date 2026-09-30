#pragma once
#include <core/load_error.h>
#include <core/rule.h>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <expected>
#include <optional>

namespace tc {

// header: the file's leading comments, verbatim. generatedUuids: some rule got
// a fresh id, so the caller should write the file back.
struct RuleFile {
    QList<Rule> rules;
    QStringList header;
    QList<LoadError> errors;
    bool generatedUuids = false;
    QString eol = QStringLiteral("\n");
    bool trailingNewline = true;
};

// A rule with a bad action is kept as skip, with the problem in errors.
std::expected<RuleFile, LoadError> readRules(const QString& path);
std::expected<void, LoadError> writeRules(const RuleFile& file, const QString& path);

// anyTag(facets: A, B) AND NOT anyTag(name: "x*") OR anyTag(facets: C)
// Malformed clauses are dropped and named in errors.
RuleMatch parseMatch(const QString& expr, QStringList* errors = nullptr);
QString serializeMatch(const RuleMatch& match);

// skip | delete | add("a", "b") | replace("a", "b") | flag(label)
std::optional<RuleAction> parseAction(const QString& expr);
QString serializeAction(const RuleAction& action);

// Saved states carry whole rules, for machines whose rules.fct lacks them.
QJsonObject ruleToJson(const Rule& rule);
Rule ruleFromJson(const QJsonObject& obj);

} // namespace tc
