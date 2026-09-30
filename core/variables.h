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

// vars.fct: one `$NAME$ = value` per line. Order is the file's order and is
// preserved on write, since the composer sidebar lets you arrange them.
class Variables {
public:
    QString value(const QString& name) const;
    bool isDefined(const QString& name) const;
    // Appends when the name is new, so this is also how one is added.
    void set(const QString& name, const QString& value);
    bool remove(const QString& name);
    void setAll(QList<Variable> vars);
    const QList<Variable>& all() const;

    // Substitutes every $NAME$. A name that resolves to nothing takes an
    // adjacent space or hyphen with it, so "blue $CC$ eyes" becomes
    // "blue eyes" rather than "blue  eyes".
    QString expand(const QString& tag) const;

private:
    QList<Variable> m_vars;
};

// header holds the file's leading comment and blank lines verbatim. Definitions
// are regenerated on write, so a comment between two of them is not kept.
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

// Removes every $NAME$ token without consulting any definitions. Used as a
// display fallback and to find a tag's facets by its pre-expansion name.
QString stripVariables(const QString& tag);

// Every $NAME$ token in `tags` that vars.fct does not define. Expansion strips
// those silently, so this is how a typo becomes visible.
struct VarIssue {
    QString tag;
    QString name;
};

QList<VarIssue> undefinedVariables(const QStringList& tags, const Variables& vars);

} // namespace tc
