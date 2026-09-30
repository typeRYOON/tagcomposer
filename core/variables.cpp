#include <core/variables.h>
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

const QRegularExpression& tokenRe()
{
    static const QRegularExpression re(uR"(\$([A-Za-z0-9_]+)\$)"_s);
    return re;
}

const QRegularExpression& stripRe()
{
    static const QRegularExpression re(uR"(\$[A-Za-z0-9_]+\$[ -]?)"_s);
    return re;
}

const QRegularExpression& lineRe()
{
    static const QRegularExpression re(uR"(^\$([A-Za-z0-9_]+)\$\s*=\s*(.*)$)"_s);
    return re;
}

} // namespace

QString Variables::value(const QString& name) const
{
    for (const Variable& v : m_vars)
        if (v.name == name) return v.value;
    return {};
}

bool Variables::isDefined(const QString& name) const
{
    for (const Variable& v : m_vars)
        if (v.name == name) return true;
    return false;
}

void Variables::set(const QString& name, const QString& value)
{
    if (name.isEmpty()) return;
    for (Variable& v : m_vars) {
        if (v.name == name) {
            v.value = value;
            return;
        }
    }
    m_vars << Variable{name, value};
}

bool Variables::remove(const QString& name)
{
    for (qsizetype i = 0; i < m_vars.size(); ++i) {
        if (m_vars[i].name != name) continue;
        m_vars.removeAt(i);
        return true;
    }
    return false;
}

void Variables::setAll(QList<Variable> vars)
{
    m_vars = std::move(vars);
}

const QList<Variable>& Variables::all() const
{
    return m_vars;
}

QString Variables::expand(const QString& tag) const
{
    QString result = tag;
    qsizetype offset = 0;

    while (true) {
        const QRegularExpressionMatch m = tokenRe().match(result, offset);
        if (!m.hasMatch()) break;

        const QString replacement = value(m.captured(1));
        const qsizetype start = m.capturedStart();
        qsizetype end = m.capturedEnd();

        if (replacement.isEmpty()) {
            if (end < result.size() && (result[end] == u' ' || result[end] == u'-')) ++end;
            result.remove(start, end - start);
            offset = start;
        }
        else {
            result.replace(start, end - start, replacement);
            offset = start + replacement.size();
        }
    }
    return result.trimmed();
}

bool hasVariable(const QString& tag)
{
    return tokenRe().match(tag).hasMatch();
}

QString stripVariables(const QString& tag)
{
    QString result = tag;
    result.remove(stripRe());
    return result.trimmed();
}

QList<VarIssue> undefinedVariables(const QStringList& tags, const Variables& vars)
{
    QList<VarIssue> out;
    for (const QString& tag : tags) {
        QRegularExpressionMatchIterator it = tokenRe().globalMatch(tag);
        while (it.hasNext()) {
            const QString name = it.next().captured(1);
            if (!vars.isDefined(name)) out << VarIssue{tag, name};
        }
    }
    return out;
}

std::expected<VariablesFile, LoadError> readVariables(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::unexpected(LoadError{path, "cannot open: " + f.errorString()});

    const QString body = QString::fromUtf8(f.readAll());

    VariablesFile out;
    out.eol = body.contains("\r\n"_L1) ? u"\r\n"_s : u"\n"_s;
    out.trailingNewline = body.endsWith(u'\n');

    QStringList lines = body.split(u'\n');
    if (body.endsWith(u'\n') && !lines.isEmpty()) lines.removeLast();

    QList<Variable> parsed;
    bool inHeader = true;
    int lineNo = 0;

    for (QString line : lines) {
        ++lineNo;
        if (line.endsWith(u'\r')) line.chop(1);
        const QString text = line.trimmed();

        if (text.isEmpty() || text.startsWith(u'#')) {
            if (inHeader) out.header << line;
            continue;
        }
        inHeader = false;

        const QRegularExpressionMatch m = lineRe().match(text);
        if (!m.hasMatch()) {
            out.warnings << LoadError{QString("%1:%2").arg(path).arg(lineNo),
                                      u"not a $NAME$ = value line: "_s + text};
            continue;
        }

        const QString name = m.captured(1);
        bool dup = false;
        for (const Variable& v : parsed)
            if (v.name == name) dup = true;

        if (dup) {
            out.warnings << LoadError{QString("%1:%2").arg(path).arg(lineNo),
                                      u"duplicate variable, first wins: $"_s + name + u"$"_s};
            continue;
        }
        parsed << Variable{name, m.captured(2).trimmed()};
    }

    out.vars.setAll(std::move(parsed));
    return out;
}

std::expected<void, LoadError> writeVariables(const VariablesFile& file, const QString& path)
{
    QString out;

    for (const QString& h : file.header) {
        out += h;
        out += file.eol;
    }
    for (const Variable& v : file.vars.all()) {
        out += u"$"_s + v.name + u"$ = "_s + v.value;
        out += file.eol;
    }

    if (!file.trailingNewline && out.endsWith(file.eol)) out.chop(file.eol.size());

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return std::unexpected(LoadError{path, "cannot open for writing: " + f.errorString()});

    f.write(out.toUtf8());
    if (!f.commit()) return std::unexpected(LoadError{path, "write failed: " + f.errorString()});

    return {};
}

} // namespace tc
