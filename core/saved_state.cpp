#include <core/saved_state.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace tc {

QJsonObject entryPushToJson(const EntryPush& push)
{
    QJsonArray tagArray;
    for (const QString& tag : push.tags)
        tagArray.append(tag);

    QJsonObject obj;
    obj[u"uuid"_s] = push.entryUuid;
    obj[u"imageFileName"_s] = push.imageFile;
    obj[u"tags"_s] = tagArray;
    return obj;
}

EntryPush entryPushFromJson(const QJsonObject& obj)
{
    EntryPush push;
    push.entryUuid = obj[u"uuid"_s].toString();
    push.imageFile = obj[u"imageFileName"_s].toString();
    for (const QJsonValue tag : obj[u"tags"_s].toArray())
        push.tags << tag.toString();
    return push;
}

SavedState SavedState::fromJson(const QJsonObject& obj)
{
    SavedState state;
    state.id = obj[u"id"_s].toString();
    state.name = obj[u"name"_s].toString();
    state.createdAt = obj.contains(u"createdAt"_s) ? obj[u"createdAt"_s].toInteger()
                                                   : state.id.toLongLong();

    for (const QJsonValue tag : obj[u"activeTags"_s].toArray())
        state.activeTags << tag.toString();

    const QJsonObject weights = obj[u"tagWeights"_s].toObject();
    for (auto it = weights.constBegin(); it != weights.constEnd(); ++it)
        state.tagWeights[it.key()] = float(it.value().toDouble(1.0));

    for (const QJsonValue tag : obj[u"deactivatedTags"_s].toArray())
        state.deactivatedTags.insert(tag.toString());

    const QJsonObject categories = obj[u"deactivatedCategory"_s].toObject();
    for (auto it = categories.constBegin(); it != categories.constEnd(); ++it)
        state.deactivatedCategory[it.key()] = it.value().toString();

    for (const QJsonValue push : obj[u"activePushes"_s].toArray())
        state.activePushes << entryPushFromJson(push.toObject());

    const QJsonObject customFacets = obj[u"customTagFacets"_s].toObject();
    for (auto it = customFacets.constBegin(); it != customFacets.constEnd(); ++it) {
        QStringList facets;
        for (const QJsonValue facet : it.value().toArray())
            facets << facet.toString();
        if (!facets.isEmpty()) state.customTagFacets[it.key()] = facets;
    }

    const QJsonObject rules = obj[u"ruleStates"_s].toObject();
    for (auto it = rules.constBegin(); it != rules.constEnd(); ++it)
        state.ruleStates[it.key()] = it.value().toBool();

    const QJsonObject ruleArguments = obj[u"ruleArguments"_s].toObject();
    for (auto it = ruleArguments.constBegin(); it != ruleArguments.constEnd(); ++it) {
        QStringList arguments;
        for (const QJsonValue argument : it.value().toArray())
            arguments << argument.toString();
        state.ruleArguments[it.key()] = arguments;
    }

    state.rulesSnapshot = obj[u"rulesSnapshot"_s].toArray();

    // The array form keeps the user's order. The object form is legacy and
    // reorders once, on the next save.
    {
        const QJsonValue values = obj[u"varValues"_s];
        if (values.isArray()) {
            for (const QJsonValue value : values.toArray()) {
                const QJsonObject one = value.toObject();
                state.varValues.append({one[u"name"_s].toString(), one[u"value"_s].toString()});
            }
        } else {
            const QJsonObject legacy = values.toObject();
            for (auto it = legacy.constBegin(); it != legacy.constEnd(); ++it)
                state.varValues.append({it.key(), it.value().toString()});
        }
    }

    state.selectedWorkflowId = obj[u"selectedWorkflowId"_s].toString();

    {
        // Legacy object form: the placeholder was the key.
        const QJsonValue values = obj[u"workflowVarValues"_s];
        if (values.isArray()) {
            state.workflowVarValues = values.toArray();
        } else if (values.isObject()) {
            QJsonArray rebuilt;
            const QJsonObject legacy = values.toObject();
            for (auto it = legacy.constBegin(); it != legacy.constEnd(); ++it) {
                QJsonObject entry = it.value().toObject();
                entry[u"placeholder"_s] = it.key();
                rebuilt.append(entry);
            }
            state.workflowVarValues = rebuilt;
        }
    }

    state.previewImagePath = obj[u"previewImage"_s].toString();
    for (const QJsonValue uuid : obj[u"activeLoraUuids"_s].toArray())
        state.activeLoraUuids << uuid.toString();

    // Absent on a legacy state, and a restore must then leave the live
    // profiles alone rather than clearing them.
    state.profilesStamped =
        obj.contains(u"groupProfile"_s) || obj.contains(u"formatProfile"_s);
    if (state.profilesStamped) {
        const QJsonObject groupProfile = obj[u"groupProfile"_s].toObject();
        state.groupProfileName = groupProfile[u"name"_s].toString();
        for (const QJsonValue name : groupProfile[u"order"_s].toArray())
            state.groupOrder << name.toString();

        const QJsonObject formatProfile = obj[u"formatProfile"_s].toObject();
        state.formatProfileName = formatProfile[u"name"_s].toString();
        for (const QJsonValue value : formatProfile[u"formats"_s].toArray()) {
            const QJsonObject one = value.toObject();
            const QString facet = one[u"facet"_s].toString().trimmed();
            if (facet.isEmpty()) continue;
            state.facetFormats << FacetFormat{facet, one[u"prefix"_s].toString(),
                                              one[u"suffix"_s].toString()};
        }
    }

    return state;
}

QJsonObject SavedState::toJson() const
{
    QJsonObject obj;
    obj[u"id"_s] = id;
    obj[u"name"_s] = name;
    obj[u"createdAt"_s] = createdAt;

    QJsonArray tagArray;
    for (const QString& tag : activeTags)
        tagArray.append(tag);
    obj[u"activeTags"_s] = tagArray;

    QJsonObject weightObject;
    for (auto it = tagWeights.constBegin(); it != tagWeights.constEnd(); ++it)
        weightObject[it.key()] = double(it.value());
    obj[u"tagWeights"_s] = weightObject;

    QJsonArray deactivatedArray;
    for (const QString& tag : deactivatedTags)
        deactivatedArray.append(tag);
    obj[u"deactivatedTags"_s] = deactivatedArray;

    QJsonObject categoryObject;
    for (auto it = deactivatedCategory.constBegin(); it != deactivatedCategory.constEnd(); ++it)
        categoryObject[it.key()] = it.value();
    obj[u"deactivatedCategory"_s] = categoryObject;

    QJsonArray pushArray;
    for (const EntryPush& push : activePushes)
        pushArray.append(entryPushToJson(push));
    obj[u"activePushes"_s] = pushArray;

    QJsonObject customFacetObject;
    for (auto it = customTagFacets.constBegin(); it != customTagFacets.constEnd(); ++it) {
        QJsonArray facets;
        for (const QString& facet : it.value())
            facets.append(facet);
        customFacetObject[it.key()] = facets;
    }
    obj[u"customTagFacets"_s] = customFacetObject;

    QJsonObject ruleObject;
    for (auto it = ruleStates.constBegin(); it != ruleStates.constEnd(); ++it)
        ruleObject[it.key()] = it.value();
    obj[u"ruleStates"_s] = ruleObject;

    QJsonObject ruleArgumentObject;
    for (auto it = ruleArguments.constBegin(); it != ruleArguments.constEnd(); ++it) {
        QJsonArray arguments;
        for (const QString& argument : it.value())
            arguments.append(argument);
        ruleArgumentObject[it.key()] = arguments;
    }
    obj[u"ruleArguments"_s] = ruleArgumentObject;
    obj[u"rulesSnapshot"_s] = rulesSnapshot;

    QJsonArray varArray;
    for (const QPair<QString, QString>& value : varValues) {
        QJsonObject one;
        one[u"name"_s] = value.first;
        one[u"value"_s] = value.second;
        varArray.append(one);
    }
    obj[u"varValues"_s] = varArray;

    obj[u"selectedWorkflowId"_s] = selectedWorkflowId;
    obj[u"workflowVarValues"_s] = workflowVarValues;

    // Stored as a bare file name; loadFromDir puts the directory back.
    obj[u"previewImage"_s] =
        previewImagePath.isEmpty() ? QString() : QFileInfo(previewImagePath).fileName();

    QJsonArray loraArray;
    for (const QString& uuid : activeLoraUuids)
        loraArray.append(uuid);
    obj[u"activeLoraUuids"_s] = loraArray;

    // Written only when stamped, so rewriting a legacy state does not turn
    // "no opinion" into an empty stamp.
    if (profilesStamped) {
        QJsonArray orderArray;
        for (const QString& name : groupOrder)
            orderArray.append(name);

        QJsonObject groupProfile;
        groupProfile[u"name"_s] = groupProfileName;
        groupProfile[u"order"_s] = orderArray;
        obj[u"groupProfile"_s] = groupProfile;

        QJsonArray formatArray;
        for (const FacetFormat& format : facetFormats) {
            QJsonObject one;
            one[u"facet"_s] = format.facet;
            one[u"prefix"_s] = format.prefix;
            one[u"suffix"_s] = format.suffix;
            formatArray.append(one);
        }

        QJsonObject formatProfile;
        formatProfile[u"name"_s] = formatProfileName;
        formatProfile[u"formats"_s] = formatArray;
        obj[u"formatProfile"_s] = formatProfile;
    }

    return obj;
}

QList<SavedState>& StateManager::states()
{
    return m_states;
}

const QList<SavedState>& StateManager::states() const
{
    return m_states;
}

StateManager StateManager::loadFromDir(const QString& dir)
{
    StateManager manager;

    const QDir root(dir);
    if (!root.exists()) return manager;

    for (const QString& sub : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QFile file(dir + u"/"_s + sub + u"/state.json"_s);
        if (!file.open(QIODevice::ReadOnly)) continue;

        SavedState state = SavedState::fromJson(QJsonDocument::fromJson(file.readAll()).object());
        if (!state.previewImagePath.isEmpty()) {
            const QString full = dir + u"/"_s + sub + u"/"_s + state.previewImagePath;
            state.previewImagePath = QFile::exists(full) ? full : QString();
        }
        manager.m_states << state;
    }

    std::stable_sort(
        manager.m_states.begin(), manager.m_states.end(),
        [](const SavedState& a, const SavedState& b) { return a.createdAt > b.createdAt; });
    return manager;
}

void StateManager::saveToDir(const QString& dir) const
{
    QDir().mkpath(dir);

    for (const SavedState& state : m_states) {
        const QString stateDir = dir + u"/"_s + state.id;
        QDir().mkpath(stateDir);

        QFile file(stateDir + u"/state.json"_s);
        if (file.open(QIODevice::WriteOnly))
            file.write(QJsonDocument(state.toJson()).toJson());
    }
}

} // namespace tc
