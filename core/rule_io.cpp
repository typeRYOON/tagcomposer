#include <core/rule_io.h>
#include <core/fct.h>
#include <QJsonArray>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

QStringList splitTopLevel(const QString& expr, QLatin1StringView sep)
{
    QStringList out;
    int depth = 0;
    qsizetype start = 0;

    for (qsizetype i = 0; i < expr.size(); ++i) {
        if (expr[i] == u'(') {
            ++depth;
            continue;
        }
        if (expr[i] == u')') {
            --depth;
            continue;
        }
        if (depth == 0 && QStringView(expr).sliced(i).startsWith(sep)) {
            out << expr.sliced(start, i - start).trimmed();
            i += sep.size() - 1;
            start = i + 1;
        }
    }
    out << expr.sliced(start).trimmed();
    return out;
}

std::optional<MatchClause> parseClause(const QString& raw)
{
    MatchClause c;
    QString expr = raw.trimmed();

    if (expr.startsWith("NOT "_L1)) {
        c.negate = true;
        expr = expr.sliced(4).trimmed();
    }

    if (!expr.startsWith("anyTag("_L1) || !expr.endsWith(u')')) return std::nullopt;

    const QString inner = expr.sliced(7, expr.size() - 8).trimmed();

    if (inner.startsWith("facets:"_L1)) {
        c.type = MatchType::AnyTagFacets;
        c.facets = splitList(QStringView(inner).sliced(7));
        if (c.facets.isEmpty()) return std::nullopt;
    }
    else if (inner.startsWith("name:"_L1)) {
        c.type = MatchType::AnyTagName;
        c.nameGlob = inner.sliced(5).trimmed().remove(u'"');
        if (c.nameGlob.isEmpty()) return std::nullopt;
    }
    else {
        return std::nullopt;
    }

    return c;
}

QString serializeClause(const MatchClause& c)
{
    QString s;
    if (c.negate) s += u"NOT "_s;

    switch (c.type) {
    case MatchType::AnyTagFacets:
        s += u"anyTag(facets: "_s + c.facets.join(u", "_s) + u")"_s;
        break;
    case MatchType::AnyTagName:
        s += u"anyTag(name: \""_s + c.nameGlob + u"\")"_s;
        break;
    }
    return s;
}

QStringList extractArgs(const QString& expr)
{
    const qsizetype l = expr.indexOf(u'(');
    const qsizetype r = expr.lastIndexOf(u')');
    if (l < 0 || r <= l) return {};

    QStringList args;
    QString current;
    bool inQuote = false;

    for (const QChar ch : expr.sliced(l + 1, r - l - 1)) {
        if (ch == u'"') {
            inQuote = !inQuote;
        }
        else if (ch == u',' && !inQuote) {
            if (const QString t = current.trimmed(); !t.isEmpty()) args << t;
            current.clear();
        }
        else {
            current += ch;
        }
    }
    if (const QString t = current.trimmed(); !t.isEmpty()) args << t;
    return args;
}

QString joinArgs(const QString& fn, const QStringList& args, bool quote)
{
    QStringList parts;
    for (const QString& a : args)
        parts << (quote ? u"\""_s + a + u"\""_s : a);
    return fn + u"("_s + parts.join(u", "_s) + u")"_s;
}

} // namespace

RuleMatch parseMatch(const QString& expr, QStringList* errors)
{
    RuleMatch m;
    for (const QString& orPart : splitTopLevel(expr, " OR "_L1)) {
        QList<MatchClause> group;
        for (const QString& andPart : splitTopLevel(orPart, " AND "_L1)) {
            const std::optional<MatchClause> c = parseClause(andPart);
            if (!c) {
                if (errors) *errors << u"malformed clause \""_s + andPart + u"\""_s;
                continue;
            }
            group << *c;
        }
        if (!group.isEmpty()) m.orGroups << group;
    }
    return m;
}

QString serializeMatch(const RuleMatch& match)
{
    QStringList orParts;
    for (const QList<MatchClause>& group : match.orGroups) {
        QStringList andParts;
        for (const MatchClause& c : group)
            andParts << serializeClause(c);
        orParts << andParts.join(u" AND "_s);
    }
    return orParts.join(u" OR "_s);
}

std::optional<RuleAction> parseAction(const QString& expr)
{
    RuleAction a;

    if (expr == "skip"_L1) {
        a.type = ActionType::Skip;
        return a;
    }
    if (expr == "delete"_L1) {
        a.type = ActionType::Delete;
        return a;
    }
    if (expr.startsWith("add("_L1)) {
        a.type = ActionType::Add;
        a.arguments = extractArgs(expr);
        return a;
    }
    if (expr.startsWith("replace("_L1)) {
        a.type = ActionType::Replace;
        a.arguments = extractArgs(expr);
        return a;
    }
    if (expr.startsWith("flag("_L1)) {
        a.type = ActionType::Flag;
        a.arguments = extractArgs(expr);
        return a;
    }
    return std::nullopt;
}

QString serializeAction(const RuleAction& action)
{
    switch (action.type) {
    case ActionType::Skip:
        return u"skip"_s;
    case ActionType::Delete:
        return u"delete"_s;
    case ActionType::Add:
        return joinArgs(u"add"_s, action.arguments, true);
    case ActionType::Replace:
        return joinArgs(u"replace"_s, action.arguments, true);
    case ActionType::Flag:
        return joinArgs(u"flag"_s, action.arguments, false);
    }
    return u"skip"_s;
}

std::expected<RuleFile, LoadError> readRules(const QString& path)
{
    const std::expected<FctDoc, LoadError> doc = readFct(path);
    if (!doc) return std::unexpected(doc.error());

    RuleFile out;
    out.eol = doc->eol;
    out.trailingNewline = doc->trailingNewline;
    QSet<QString> seenUuids;

    for (const FctBlock& b : doc->blocks) {
        if (b.kind.isEmpty()) {
            for (const FctLine& l : b.lines) {
                if (!l.trivia) break;
                out.header << l.raw;
            }
            continue;
        }
        if (b.kind != "rule"_L1) continue;

        if (b.name.isEmpty()) {
            out.errors << LoadError{path, u"rule with an empty name, skipped"_s};
            continue;
        }

        Rule r;
        r.name = b.name;
        bool actionSet = false;

        for (const FctLine& l : b.lines) {
            if (l.trivia || l.key.isEmpty()) continue;
            const QString val = l.values.value(0);

            if (l.key == "id"_L1) {
                r.uuid = val;
            }
            else if (l.key == "enabled"_L1) {
                r.enabled = (val == "true"_L1);
            }
            else if (l.key == "force"_L1) {
                r.force = (val == "true"_L1);
            }
            else if (l.key == "match"_L1) {
                QStringList errs;
                r.match = parseMatch(val, &errs);
                for (const QString& e : errs)
                    out.errors << LoadError{path, u"rule \""_s + r.name + u"\": "_s + e};
            }
            else if (l.key == "action"_L1) {
                const std::optional<RuleAction> a = parseAction(val);
                if (!a) {
                    out.errors << LoadError{
                        path, u"rule \""_s + r.name + u"\": unknown action \""_s + val + u"\""_s};
                }
                else {
                    r.action = *a;
                    actionSet = true;
                }
            }
        }

        if (!actionSet)
            out.errors << LoadError{path, u"rule \""_s + r.name + u"\": missing action"_s};

        if (r.uuid.isEmpty() || seenUuids.contains(r.uuid)) {
            r.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
            out.generatedUuids = true;
        }
        seenUuids.insert(r.uuid);
        out.rules << r;
    }

    return out;
}

std::expected<void, LoadError> writeRules(const RuleFile& file, const QString& path)
{
    QString out;

    for (const QString& h : file.header) {
        out += h;
        out += file.eol;
    }

    for (const Rule& r : file.rules) {
        out += u"@rule "_s + r.name + file.eol;
        out += u"    id      = "_s + r.uuid + file.eol;
        out += u"    enabled = "_s + (r.enabled ? u"true"_s : u"false"_s) + file.eol;
        if (r.force) out += u"    force   = true"_s + file.eol;
        out += u"    match   = "_s + serializeMatch(r.match) + file.eol;
        out += u"    action  = "_s + serializeAction(r.action) + file.eol;
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

namespace {

QString matchTypeToString(MatchType type)
{
    return type == MatchType::AnyTagName ? u"name"_s : u"facets"_s;
}

MatchType matchTypeFromString(const QString& text)
{
    return text == "name"_L1 ? MatchType::AnyTagName : MatchType::AnyTagFacets;
}

QString actionTypeToString(ActionType type)
{
    switch (type) {
    case ActionType::Skip:
        return u"skip"_s;
    case ActionType::Add:
        return u"add"_s;
    case ActionType::Replace:
        return u"replace"_s;
    case ActionType::Flag:
        return u"flag"_s;
    case ActionType::Delete:
        return u"delete"_s;
    }
    return u"skip"_s;
}

ActionType actionTypeFromString(const QString& text)
{
    if (text == "add"_L1) return ActionType::Add;
    if (text == "replace"_L1) return ActionType::Replace;
    if (text == "flag"_L1) return ActionType::Flag;
    if (text == "delete"_L1) return ActionType::Delete;
    return ActionType::Skip;
}

} // namespace

QJsonObject ruleToJson(const Rule& rule)
{
    QJsonArray orGroups;
    for (const QList<MatchClause>& group : rule.match.orGroups) {
        QJsonArray andGroup;
        for (const MatchClause& clause : group) {
            QJsonObject entry;
            entry[u"negate"_s] = clause.negate;
            entry[u"type"_s] = matchTypeToString(clause.type);

            if (clause.type == MatchType::AnyTagFacets) {
                QJsonArray facets;
                for (const QString& facet : clause.facets)
                    facets.append(facet);
                entry[u"facets"_s] = facets;
            } else {
                entry[u"nameGlob"_s] = clause.nameGlob;
            }
            andGroup.append(entry);
        }
        orGroups.append(andGroup);
    }

    QJsonArray arguments;
    for (const QString& argument : rule.action.arguments)
        arguments.append(argument);

    QJsonObject match;
    match[u"orGroups"_s] = orGroups;

    QJsonObject action;
    action[u"type"_s] = actionTypeToString(rule.action.type);
    action[u"arguments"_s] = arguments;

    QJsonObject obj;
    obj[u"uuid"_s] = rule.uuid;
    obj[u"name"_s] = rule.name;
    obj[u"enabled"_s] = rule.enabled;
    obj[u"force"_s] = rule.force;
    obj[u"match"_s] = match;
    obj[u"action"_s] = action;
    return obj;
}

Rule ruleFromJson(const QJsonObject& obj)
{
    Rule rule;
    rule.uuid = obj[u"uuid"_s].toString();
    rule.name = obj[u"name"_s].toString();
    rule.enabled = obj[u"enabled"_s].toBool();
    rule.force = obj[u"force"_s].toBool();

    for (const QJsonValue orValue : obj[u"match"_s].toObject()[u"orGroups"_s].toArray()) {
        QList<MatchClause> group;
        for (const QJsonValue andValue : orValue.toArray()) {
            const QJsonObject entry = andValue.toObject();

            MatchClause clause;
            clause.negate = entry[u"negate"_s].toBool();
            clause.type = matchTypeFromString(entry[u"type"_s].toString());

            if (clause.type == MatchType::AnyTagFacets) {
                for (const QJsonValue facet : entry[u"facets"_s].toArray())
                    clause.facets << facet.toString();
            } else {
                clause.nameGlob = entry[u"nameGlob"_s].toString();
            }
            group << clause;
        }
        if (!group.isEmpty()) rule.match.orGroups << group;
    }

    const QJsonObject action = obj[u"action"_s].toObject();
    rule.action.type = actionTypeFromString(action[u"type"_s].toString());
    for (const QJsonValue argument : action[u"arguments"_s].toArray())
        rule.action.arguments << argument.toString();

    return rule;
}

} // namespace tc
