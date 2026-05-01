#include <core/variableindex.h>
#include <QFile>
#include <QSet>
#include <QTextStream>
#include <QRegularExpression>

namespace core {

static const QRegularExpression k_varRe(R"(\$([A-Za-z0-9_]+)\$)");
static const QRegularExpression k_stripRe(R"(\$[A-Za-z0-9_]+\$[ -]?)");

VariableIndex VariableIndex::loadFromFile(const QString& path)
{
    VariableIndex idx;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return idx;

    static const QRegularExpression lineRe(
        R"(^\$([A-Za-z0-9_]+)\$\s*=\s*(.*)$)"
    );
    QSet<QString> seenNames;
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();

        if (line.isEmpty() || line.startsWith('#')) continue;
        const auto m = lineRe.match(line);
        if (!m.hasMatch()) continue;
        const QString name = m.captured(1);
        if (seenNames.contains(name)) continue;  // first definition wins
        seenNames.insert(name);
        idx.m_variables << Variable{ name, m.captured(2).trimmed() };
    }
    return idx;
}

void VariableIndex::saveToFile(const QString& path) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    QTextStream out(&f);
    for (const Variable& v : m_variables)
        out << "$" << v.name << "$ = " << v.value << "\n";
}

QString VariableIndex::expand(const QString& tag) const
{
    QString result = tag;
    int offset = 0;
    QRegularExpressionMatch m;
    while ((m = k_varRe.match(result, offset)).hasMatch()) {
        const QString varName = m.captured(1);
        QString replacement;
        for (const Variable& v : m_variables)
            if (v.name == varName) { replacement = v.value; break; }

        const int start = m.capturedStart();
        int       end   = m.capturedEnd();

        if (replacement.isEmpty()) {
            if (end < result.size() && (result[end] == ' ' || result[end] == '-'))
                ++end;
            result.remove(start, end - start);
            offset = start;
        } else {
            result.replace(start, end - start, replacement);
            offset = start + replacement.size();
        }
    }
    return result.trimmed();
}

QString VariableIndex::stripVariables(const QString& tag)
{
    QString result = tag;
    result.remove(k_stripRe);
    return result.trimmed();
}

bool VariableIndex::hasVariable(const QString& tag)
{
    return k_varRe.match(tag).hasMatch();
}

QList<Variable>&       VariableIndex::variables()       { return m_variables; }
const QList<Variable>& VariableIndex::variables() const { return m_variables; }

Variable* VariableIndex::find(const QString& name)
{
    for (Variable& v : m_variables)
        if (v.name == name) return &v;
    return nullptr;
}

} // namespace core
