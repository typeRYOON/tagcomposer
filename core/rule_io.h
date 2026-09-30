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

// header holds the file's leading comment and blank lines verbatim.
// generatedUuids is set when a rule on disk had no id, or a duplicate one, and
// a fresh id was made for it; the caller should write the file back so ids stop
// moving between runs.
struct RuleFile {
    QList<Rule> rules;
    QStringList header;
    QList<LoadError> errors;
    bool generatedUuids = false;
    QString eol = QStringLiteral("\n");
    bool trailingNewline = true;
};

// A rule with an unparseable action is still returned, with its action left as
// skip and the problem in errors.
std::expected<RuleFile, LoadError> readRules(const QString& path);
std::expected<void, LoadError> writeRules(const RuleFile& file, const QString& path);

// anyTag(facets: A, B) AND NOT anyTag(name: "x*") OR anyTag(facets: C)
// Malformed clauses are dropped and named in errors.
RuleMatch parseMatch(const QString& expr, QStringList* errors = nullptr);
QString serializeMatch(const RuleMatch& match);

// skip | delete | add("a", "b") | replace("a", "b") | flag(label)
std::optional<RuleAction> parseAction(const QString& expr);
QString serializeAction(const RuleAction& action);

// A whole rule as JSON, which is how a saved state carries one so it can be
// recreated on a machine whose rules.fct never had it.
QJsonObject ruleToJson(const Rule& rule);
Rule ruleFromJson(const QJsonObject& obj);

} // namespace tc
