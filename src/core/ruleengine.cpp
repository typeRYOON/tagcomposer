#include <core/ruleengine.h>
#include <core/facetindex.h>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>
#include <QRegularExpression>
#include <QUuid>

namespace core {

// ---- Helpers

static QList<QString> splitTrimmed(const QString& s, QChar sep)
{
    QList<QString> out;
    for (const QString& p : s.split(sep))
        if (const QString t = p.trimmed(); !t.isEmpty()) out << t;
    return out;
}

// Top-level split (depth 0); parens-internal " AND "/" OR " stay intact.
static QList<QString> splitTopLevel(const QString& expr, const QString& sep)
{
    QList<QString> out;
    int depth = 0;
    int start = 0;
    for (int i = 0; i < expr.size(); ++i) {
        if (expr[i] == '(') {
            ++depth;
            continue;
        }
        if (expr[i] == ')') {
            --depth;
            continue;
        }
        if (depth == 0 && expr.mid(i, sep.size()) == sep) {
            out << expr.mid(start, i - start).trimmed();
            i += sep.size() - 1;
            start = i + 1;
        }
    }
    out << expr.mid(start).trimmed();
    return out;
}

// [NOT] anyTag(facets: ...) | anyTag(name: "..."). Sets *ok=false on
// malformed input; caller skips bad clauses.
static MatchClause parseClause(const QString& raw, bool* ok)
{
    MatchClause c;
    QString expr = raw.trimmed();

    if (expr.startsWith("NOT ")) {
        c.negate = true;
        expr = expr.mid(4).trimmed();
    }

    if (!expr.startsWith("anyTag(") || !expr.endsWith(')')) {
        *ok = false;
        return c;
    }
    const QString inner = expr.mid(7, expr.length() - 8).trimmed();

    if (inner.startsWith("facets:")) {
        c.type = MatchType::AnyTagFacets;
        c.facets = splitTrimmed(inner.mid(7), ',');
        *ok = !c.facets.isEmpty();
    }
    else if (inner.startsWith("name:")) {
        c.type = MatchType::AnyTagName;
        c.nameGlob = inner.mid(5).trimmed().remove('"');
        *ok = !c.nameGlob.isEmpty();
    }
    else {
        *ok = false;
    }

    return c;
}

// OR of AND-groups; AND binds tighter (standard precedence). Bad clauses
// are dropped with an error rather than left to corrupt the rule.
static RuleMatch parseMatch(const QString& expr, QStringList* errors, const QString& ruleName)
{
    RuleMatch m;
    for (const QString& orPart : splitTopLevel(expr, " OR ")) {
        QList<MatchClause> group;
        for (const QString& andPart : splitTopLevel(orPart, " AND ")) {
            bool ok = true;
            MatchClause c = parseClause(andPart, &ok);
            if (!ok) {
                if (errors)
                    *errors << QString("Rule \"%1\": malformed clause \"%2\"")
                                   .arg(ruleName, andPart);
                continue;
            }
            group << c;
        }
        if (!group.isEmpty()) m.orGroups << group;
    }
    return m;
}

static QString serializeClause(const MatchClause& c)
{
    QString s;
    if (c.negate) s += "NOT ";
    switch (c.type) {
    case MatchType::AnyTagFacets:
        s += "anyTag(facets: " + c.facets.join(", ") + ")";
        break;
    case MatchType::AnyTagName:
        s += "anyTag(name: \"" + c.nameGlob + "\")";
        break;
    }
    return s;
}

// Comma-split args from add("a", "b") / flag(name) - quotes optional.
static QList<QString> extractArgs(const QString& expr)
{
    const int l = expr.indexOf('(');
    const int r = expr.lastIndexOf(')');
    if (l < 0 || r <= l) return {};

    QList<QString> args;
    bool inQuote = false;
    QString current;
    for (QChar c : expr.mid(l + 1, r - l - 1)) {
        if (c == '"') {
            inQuote = !inQuote;
        }
        else if (c == ',' && !inQuote) {
            if (const QString t = current.trimmed(); !t.isEmpty()) args << t;
            current.clear();
        }
        else {
            current += c;
        }
    }
    if (const QString t = current.trimmed(); !t.isEmpty()) args << t;
    return args;
}

static RuleAction parseAction(const QString& expr)
{
    RuleAction a;

    // Arg-taking keywords need the "(" so "addmore(...)" doesn't match "add".
    if (expr == "skip")
        a.type = ActionType::Skip;
    else if (expr == "delete")
        a.type = ActionType::Delete;
    else if (expr.startsWith("add(")) {
        a.type = ActionType::Add;
        a.arguments = extractArgs(expr);
    }
    else if (expr.startsWith("replace(")) {
        a.type = ActionType::Replace;
        a.arguments = extractArgs(expr);
    }
    else if (expr.startsWith("flag(")) {
        a.type = ActionType::Flag;
        a.arguments = extractArgs(expr);
    }

    return a;
}

// ---- Load
// File format:
//   @rule Name
//       enabled = true
//       match   = anyTag(facets: f1) AND NOT anyTag(facets: f2)
//       match   = anyTag(facets: a) OR anyTag(name: "*pattern*")
//       action  = replace("tag")

RuleEngine RuleEngine::loadFromFile(const QString& path, QStringList* errors)
{
    RuleEngine eng;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errors) *errors << QString("rules.fct: cannot open file");
        return eng;
    }

    static const QStringList knownActions = {"skip", "add", "replace", "flag", "delete"};

    Rule current;
    bool inRule{false};
    bool actionSet{false};
    bool skipBlock{false};
    bool uuidsGenerated{false};
    QSet<QString> seenUuids;

    auto finaliseRule = [&]() {
        if (skipBlock) return;
        if (!actionSet && errors)
            *errors << QString("Rule \"%1\": missing action").arg(current.name);
        if (current.uuid.isEmpty() || seenUuids.contains(current.uuid)) {
            current.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
            uuidsGenerated = true;
        }
        seenUuids.insert(current.uuid);
        eng.m_rules << current;
    };

    for (const QString& raw : QString::fromUtf8(f.readAll()).split('\n')) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;

        if (line.startsWith("@rule")) {
            if (inRule) finaliseRule();
            current = Rule{};
            actionSet = false;
            current.name = line.mid(5).trimmed();
            inRule = true;
            skipBlock = false;

            if (current.name.isEmpty()) {
                skipBlock = true;
                if (errors) *errors << "Rule with empty name skipped";
            }
            // Duplicate names are allowed; uuid is the identity. finaliseRule
            // dedupes/regenerates uuids so two same-named rules survive a save/
            // load roundtrip as long as their uuids differ.
            continue;
        }

        if (!inRule || skipBlock || !line.contains('=')) continue;

        const int eq = line.indexOf('=');
        const QString key = line.left(eq).trimmed();
        const QString val = line.mid(eq + 1).trimmed();

        if (key == "id")
            current.uuid = val;
        else if (key == "enabled")
            current.enabled = (val == "true");
        else if (key == "force")
            current.force = (val == "true");
        else if (key == "match")
            current.match = parseMatch(val, errors, current.name);
        else if (key == "action") {
            bool known = false;
            for (const QString& kw : knownActions)
                if (val == kw || val.startsWith(kw + "(")) {
                    known = true;
                    break;
                }
            if (!known && errors)
                *errors << QString("Rule \"%1\": unknown action \"%2\"").arg(current.name, val);
            current.action = parseAction(val);
            actionSet = true;
        }
    }

    if (inRule) finaliseRule();
    f.close();

    // Stabilise auto-generated uuids on disk so subsequent loads are idempotent.
    if (uuidsGenerated) eng.saveToFile(path);

    return eng;
}

// ---- Save

void RuleEngine::saveToFile(const QString& path) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;
    QTextStream ts(&f);

    for (const Rule& r : m_rules) {
        ts << "@rule " << r.name << "\n";
        ts << "    id      = " << r.uuid << "\n";
        ts << "    enabled = " << (r.enabled ? "true" : "false") << "\n";
        if (r.force) ts << "    force   = true\n";

        QStringList orParts;
        for (const QList<MatchClause>& group : r.match.orGroups) {
            QStringList andParts;
            for (const MatchClause& c : group)
                andParts << serializeClause(c);
            orParts << andParts.join(" AND ");
        }
        ts << "    match   = " << orParts.join(" OR ") << "\n";

        auto joinArgs = [&](const QString& fn, bool quote) {
            QStringList parts;
            for (const QString& arg : r.action.arguments)
                parts << (quote ? "\"" + arg + "\"" : arg);
            ts << "    action  = " << fn << "(" << parts.join(", ") << ")\n";
        };

        switch (r.action.type) {
        case ActionType::Skip:
            ts << "    action  = skip\n";
            break;
        case ActionType::Delete:
            ts << "    action  = delete\n";
            break;
        case ActionType::Add:
            joinArgs("add", true);
            break;
        case ActionType::Replace:
            joinArgs("replace", true);
            break;
        case ActionType::Flag:
            joinArgs("flag", false);
            break;
        }

        ts << "\n";
    }
}

// ---- Accessors

QList<Rule>& RuleEngine::rules()
{
    return m_rules;
}
const QList<Rule>& RuleEngine::rules() const
{
    return m_rules;
}

// ---- JSON conversion (for saved-state snapshots)

namespace {

QString matchTypeToStr(MatchType t)
{
    switch (t) {
    case MatchType::AnyTagFacets:
        return "facets";
    case MatchType::AnyTagName:
        return "name";
    }
    return "facets";
}

MatchType matchTypeFromStr(const QString& s)
{
    return s == "name" ? MatchType::AnyTagName : MatchType::AnyTagFacets;
}

QString actionTypeToStr(ActionType t)
{
    switch (t) {
    case ActionType::Skip:
        return "skip";
    case ActionType::Add:
        return "add";
    case ActionType::Replace:
        return "replace";
    case ActionType::Flag:
        return "flag";
    case ActionType::Delete:
        return "delete";
    }
    return "skip";
}

ActionType actionTypeFromStr(const QString& s)
{
    if (s == "add") return ActionType::Add;
    if (s == "replace") return ActionType::Replace;
    if (s == "flag") return ActionType::Flag;
    if (s == "delete") return ActionType::Delete;
    return ActionType::Skip;
}

} // namespace

QJsonObject RuleEngine::ruleToJson(const Rule& r)
{
    QJsonArray orArr;
    for (const QList<MatchClause>& group : r.match.orGroups) {
        QJsonArray andArr;
        for (const MatchClause& c : group) {
            QJsonObject co;
            co["negate"] = c.negate;
            co["type"] = matchTypeToStr(c.type);
            if (c.type == MatchType::AnyTagFacets) {
                QJsonArray facetsArr;
                for (const QString& f : c.facets)
                    facetsArr.append(f);
                co["facets"] = facetsArr;
            }
            else {
                co["nameGlob"] = c.nameGlob;
            }
            andArr.append(co);
        }
        orArr.append(andArr);
    }

    QJsonArray argsArr;
    for (const QString& a : r.action.arguments)
        argsArr.append(a);

    QJsonObject matchObj;
    matchObj["orGroups"] = orArr;

    QJsonObject actionObj;
    actionObj["type"] = actionTypeToStr(r.action.type);
    actionObj["arguments"] = argsArr;

    QJsonObject obj;
    obj["uuid"] = r.uuid;
    obj["name"] = r.name;
    obj["enabled"] = r.enabled;
    obj["force"] = r.force;
    obj["match"] = matchObj;
    obj["action"] = actionObj;
    return obj;
}

Rule RuleEngine::ruleFromJson(const QJsonObject& obj)
{
    Rule r;
    r.uuid = obj["uuid"].toString();
    r.name = obj["name"].toString();
    r.enabled = obj["enabled"].toBool();
    r.force = obj["force"].toBool();

    for (const QJsonValue& orVal : obj["match"].toObject()["orGroups"].toArray()) {
        QList<MatchClause> group;
        for (const QJsonValue& andVal : orVal.toArray()) {
            const QJsonObject co = andVal.toObject();
            MatchClause c;
            c.negate = co["negate"].toBool();
            c.type = matchTypeFromStr(co["type"].toString());
            if (c.type == MatchType::AnyTagFacets) {
                for (const QJsonValue& fv : co["facets"].toArray())
                    c.facets << fv.toString();
            }
            else {
                c.nameGlob = co["nameGlob"].toString();
            }
            group << c;
        }
        if (!group.isEmpty()) r.match.orGroups << group;
    }

    const QJsonObject actionObj = obj["action"].toObject();
    r.action.type = actionTypeFromStr(actionObj["type"].toString());
    for (const QJsonValue& av : actionObj["arguments"].toArray())
        r.action.arguments << av.toString();

    return r;
}

// ---- Matching

bool RuleEngine::globMatch(const QString& pattern, const QString& text)
{
    // Cache: evaluate() calls this O(tags * clauses) per push.
    static QHash<QString, QRegularExpression> cache;
    auto it = cache.find(pattern);
    if (it == cache.end()) {
        const QRegularExpression re(
            "\\A" + QRegularExpression::escape(pattern).replace("\\*", ".*").replace("\\?", ".") +
                "\\z",
            QRegularExpression::CaseInsensitiveOption);
        it = cache.insert(pattern, re);
    }
    return it.value().match(text).hasMatch();
}

bool RuleEngine::clauseMatches(const PipelineTag& pt, const MatchClause& clause)
{
    switch (clause.type) {
    case MatchType::AnyTagFacets:
        if (clause.facets.isEmpty()) return false;
        for (const QString& f : clause.facets)
            if (!pt.facets.contains(f)) return false;
        return true;

    case MatchType::AnyTagName:
        return globMatch(clause.nameGlob, pt.tag);
    }
    return false;
}

bool RuleEngine::tagMatchesRule(const PipelineTag& pt, const RuleMatch& match)
{
    if (match.orGroups.isEmpty()) return false;

    for (const QList<MatchClause>& group : match.orGroups) {
        bool groupOk = true;
        for (const MatchClause& clause : group) {
            bool ok = clauseMatches(pt, clause);
            if (clause.negate) ok = !ok;
            if (!ok) {
                groupOk = false;
                break;
            }
        }
        if (groupOk) return true;
    }
    return false;
}

// ---- Evaluate

QList<PipelineTag> RuleEngine::evaluate(const QList<PipelineTag>& input,
                                        const FacetIndex& facets) const
{
    QList<PipelineTag> working = input;

    for (const Rule& rule : m_rules) {
        if (!rule.enabled) continue;

        QList<int> matched;
        for (int i = 0; i < working.size(); ++i)
            if (working[i].result == RuleResult::Include && tagMatchesRule(working[i], rule.match))
                matched << i;

        if (matched.isEmpty() && !rule.force) continue;

        switch (rule.action.type) {
        case ActionType::Skip:
            for (int i : matched) {
                working[i].result = RuleResult::Skipped;
                working[i].ruleSource = rule.name;
            }
            break;

        case ActionType::Delete:
            for (int i : matched) {
                working[i].result = RuleResult::Deleted;
                working[i].ruleSource = rule.name;
            }
            break;

        case ActionType::Flag:
            for (int i : matched) {
                working[i].result = RuleResult::Flagged;
                working[i].flagLabel = rule.action.arguments.value(0);
                working[i].ruleSource = rule.name;
            }
            break;

        case ActionType::Add: {
            for (const QString& tag : rule.action.arguments) {
                PipelineTag injected;
                injected.tag = tag;
                injected.facets = facets.facetsFor(tag);
                injected.result = RuleResult::Injected;
                injected.ruleSource = rule.name;
                working << injected;
            }
            break;
        }

        case ActionType::Replace: {
            for (int i : matched) {
                working[i].result = RuleResult::Replaced;
                working[i].ruleSource = rule.name;
            }
            for (const QString& tag : rule.action.arguments) {
                PipelineTag injected;
                injected.tag = tag;
                injected.facets = facets.facetsFor(tag);
                injected.result = RuleResult::Injected;
                injected.ruleSource = rule.name;
                working << injected;
            }
            break;
        }
        }
    }

    return working;
}

} // namespace core
