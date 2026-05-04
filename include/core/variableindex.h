#pragma once
#include <QString>
#include <QList>

namespace core {

struct Variable {
    QString name;  // without delimiters, e.g. "CC"
    QString value; // current substitution value, e.g. "blue"
};

class VariableIndex {
public:
    static VariableIndex loadFromFile(const QString& path);
    void saveToFile(const QString& path) const;

    // Expand all $NAME$ patterns using current values.
    // When a variable has an empty value the pattern plus any adjacent
    // separator (space or hyphen immediately after) is stripped instead.
    QString expand(const QString& tag) const;

    // Strip all $NAME$ patterns (and any adjacent separator) unconditionally.
    // Used as a display/fallback base when expansion is unknown.
    static QString stripVariables(const QString& tag);

    static bool hasVariable(const QString& tag);

    QList<Variable>& variables();
    const QList<Variable>& variables() const;

    Variable* find(const QString& name);

private:
    QList<Variable> m_variables;
};

} // namespace core
