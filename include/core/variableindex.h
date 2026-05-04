#pragma once
#include <QString>
#include <QList>

namespace core {

struct Variable {
    QString name;  // without $ delimiters
    QString value;
};

class VariableIndex {
public:
    static VariableIndex loadFromFile(const QString& path);
    void saveToFile(const QString& path) const;

    // Expands $NAME$ tokens. Empty values strip the token plus an adjacent
    // space or hyphen, so "blue $CC$ eyes" with $CC$="" produces "blue eyes".
    QString expand(const QString& tag) const;

    // Unconditional strip; used as a display fallback.
    static QString stripVariables(const QString& tag);

    static bool hasVariable(const QString& tag);

    QList<Variable>& variables();
    const QList<Variable>& variables() const;

    Variable* find(const QString& name);

private:
    QList<Variable> m_variables;
};

} // namespace core
