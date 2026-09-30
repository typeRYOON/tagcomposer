#pragma once
#include <core/load_error.h>
#include <QList>
#include <QString>
#include <QStringList>
#include <expected>

namespace tc {

struct Variable {
    QString name; // without the $ delimiters
    QString value;

    bool operator==(const Variable&) const = default;
};

// vars.fct: one `$NAME$ = value` per line, order preserved.
class Variables {
public:
    QString value(const QString& name) const;
    bool isDefined(const QString& name) const;
    // Appends when the name is new.
    void set(const QString& name, const QString& value);
    bool remove(const QString& name);
    void setAll(QList<Variable> vars);
    const QList<Variable>& all() const;

    // Substitutes every $NAME$. An empty value also eats one adjacent space or
    // hyphen: "blue $CC$ eyes" -> "blue eyes".
    QString expand(const QString& tag) const;

private:
    QList<Variable> m_vars;
};

// Definitions are regenerated on write; only the leading comment block is kept.
struct VariablesFile {
    Variables vars;
    QStringList header;
    QList<LoadError> warnings;
    QString eol = QStringLiteral("\n");
    bool trailingNewline = true;
};

std::expected<VariablesFile, LoadError> readVariables(const QString& path);
std::expected<void, LoadError> writeVariables(const VariablesFile& file, const QString& path);

bool hasVariable(const QString& tag);

// Removes every $NAME$ token, ignoring definitions.
QString stripVariables(const QString& tag);

// $NAME$ tokens vars.fct doesn't define; expansion would drop them silently.
struct VarIssue {
    QString tag;
    QString name;
};

QList<VarIssue> undefinedVariables(const QStringList& tags, const Variables& vars);

} // namespace tc
