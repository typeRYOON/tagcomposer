#include <core/savedstate.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QFileInfo>

namespace core {

// ── EntryPush ─────────────────────────────────────────────────────────────────

QJsonObject EntryPush::toJson() const
{
    QJsonArray tagArr;
    for (const QString& t : tags)
        tagArr.append(t);
    QJsonObject o;
    o["uuid"] = uuid;
    o["imageFileName"] = imageFileName;
    o["tags"] = tagArr;
    return o;
}

EntryPush EntryPush::fromJson(const QJsonObject& obj)
{
    EntryPush ep;
    ep.uuid = obj["uuid"].toString();
    ep.imageFileName = obj["imageFileName"].toString();
    for (const auto& t : obj["tags"].toArray())
        ep.tags << t.toString();
    return ep;
}

// ── SavedState ────────────────────────────────────────────────────────────────

SavedState SavedState::fromJson(const QJsonObject& obj)
{
    SavedState s;
    s.id = obj["id"].toString();
    s.name = obj["name"].toString();

    for (const auto& v : obj["activeTags"].toArray())
        s.activeTags << v.toString();

    const QJsonObject weights = obj["tagWeights"].toObject();
    for (auto it = weights.constBegin(); it != weights.constEnd(); ++it)
        s.tagWeights[it.key()] = float(it.value().toDouble(1.0));

    for (const auto& v : obj["deactivatedTags"].toArray())
        s.deactivatedTags.insert(v.toString());

    for (const auto& v : obj["activePushes"].toArray())
        s.activePushes << EntryPush::fromJson(v.toObject());

    const QJsonObject rules = obj["ruleStates"].toObject();
    for (auto it = rules.constBegin(); it != rules.constEnd(); ++it)
        s.ruleStates[it.key()] = it.value().toBool();

    const QJsonObject ruleArgs = obj["ruleArguments"].toObject();
    for (auto it = ruleArgs.constBegin(); it != ruleArgs.constEnd(); ++it) {
        QList<QString> args;
        for (const auto& a : it.value().toArray())
            args << a.toString();
        s.ruleArguments[it.key()] = args;
    }

    // Array form preserves user order; object form is legacy and reorders once.
    {
        const QJsonValue vv = obj["varValues"];
        if (vv.isArray()) {
            for (const QJsonValue& v : vv.toArray()) {
                const QJsonObject o = v.toObject();
                s.varValues.append({o["name"].toString(), o["value"].toString()});
            }
        }
        else {
            const QJsonObject vars = vv.toObject();
            for (auto it = vars.constBegin(); it != vars.constEnd(); ++it)
                s.varValues.append({it.key(), it.value().toString()});
        }
    }

    s.selectedWorkflowId = obj["selectedWorkflowId"].toString();
    {
        // Legacy object form: placeholder is the key, payload is the value.
        const QJsonValue wfv = obj["workflowVarValues"];
        if (wfv.isArray()) {
            s.workflowVarValues = wfv.toArray();
        }
        else if (wfv.isObject()) {
            QJsonArray arr;
            const QJsonObject o = wfv.toObject();
            for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
                QJsonObject entry = it.value().toObject();
                entry["placeholder"] = it.key();
                arr.append(entry);
            }
            s.workflowVarValues = arr;
        }
    }
    s.previewImagePath = obj["previewImage"].toString();
    for (const auto& v : obj["activeLoraUuids"].toArray())
        s.activeLoraUuids << v.toString();
    return s;
}

QJsonObject SavedState::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["name"] = name;

    QJsonArray tagsArr;
    for (const auto& t : activeTags)
        tagsArr.append(t);
    obj["activeTags"] = tagsArr;

    QJsonObject weightsObj;
    for (auto it = tagWeights.constBegin(); it != tagWeights.constEnd(); ++it)
        weightsObj[it.key()] = double(it.value());
    obj["tagWeights"] = weightsObj;

    QJsonArray deactArr;
    for (const auto& t : deactivatedTags)
        deactArr.append(t);
    obj["deactivatedTags"] = deactArr;

    QJsonArray pushesArr;
    for (const auto& ep : activePushes)
        pushesArr.append(ep.toJson());
    obj["activePushes"] = pushesArr;

    QJsonObject rulesObj;
    for (auto it = ruleStates.constBegin(); it != ruleStates.constEnd(); ++it)
        rulesObj[it.key()] = it.value();
    obj["ruleStates"] = rulesObj;

    QJsonObject ruleArgsObj;
    for (auto it = ruleArguments.constBegin(); it != ruleArguments.constEnd(); ++it) {
        QJsonArray arr;
        for (const auto& a : it.value())
            arr.append(a);
        ruleArgsObj[it.key()] = arr;
    }
    obj["ruleArguments"] = ruleArgsObj;

    QJsonArray varsArr;
    for (const auto& v : varValues) {
        QJsonObject one;
        one["name"] = v.first;
        one["value"] = v.second;
        varsArr.append(one);
    }
    obj["varValues"] = varsArr;

    obj["selectedWorkflowId"] = selectedWorkflowId;
    obj["workflowVarValues"] = workflowVarValues;
    obj["previewImage"] =
        previewImagePath.isEmpty() ? QString() : QFileInfo(previewImagePath).fileName();
    QJsonArray loraArr;
    for (const auto& uuid : activeLoraUuids)
        loraArr.append(uuid);
    obj["activeLoraUuids"] = loraArr;
    return obj;
}

// ── StateManager ──────────────────────────────────────────────────────────────

StateManager StateManager::loadFromDir(const QString& dir)
{
    StateManager sm;
    QDir d(dir);
    if (!d.exists()) return sm;

    // One state per subdir; sort by name = chronological since IDs are timestamps.
    const QStringList subs = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& sub : subs) {
        QFile f(dir + "/" + sub + "/state.json");
        if (!f.open(QIODevice::ReadOnly)) continue;
        SavedState s = SavedState::fromJson(QJsonDocument::fromJson(f.readAll()).object());
        if (!s.previewImagePath.isEmpty()) {
            const QString full = dir + "/" + sub + "/" + s.previewImagePath;
            s.previewImagePath = QFile::exists(full) ? full : QString();
        }
        sm.m_states << s;
    }
    return sm;
}

void StateManager::saveToDir(const QString& dir) const
{
    QDir().mkpath(dir);
    for (const auto& s : m_states) {
        const QString stateDir = dir + "/" + s.id;
        QDir().mkpath(stateDir);
        QFile f(stateDir + "/state.json");
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(s.toJson()).toJson());
    }
}

} // namespace core
