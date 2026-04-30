// PromptComposerPage — session save/restore.
// Persisted on app close, loaded on app start; lives next to the main
// PromptComposerPage TU, no separate class.

#include <gui/composer/promptcomposerpage.h>
#include <core/entrymodel.h>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace gui {

void PromptComposerPage::saveSession(const QString& path) const
{
    QJsonArray tagsArr;
    for (const QString& t : m_activeTags)
        tagsArr.append(t);

    QJsonObject weightsObj;
    for (auto it = m_tagWeights.constBegin(); it != m_tagWeights.constEnd(); ++it)
        if (qAbs(it.value() - 1.0f) >= 0.001f)
            weightsObj[it.key()] = double(it.value());

    QJsonArray pushesArr;
    for (auto it = m_activePushes.constBegin(); it != m_activePushes.constEnd(); ++it) {
        const int runtimeId = int(quint32(it.key() >> 32));
        const int imageIdx  = int(quint32(it.key() & 0xFFFFFFFFLL));
        core::Entry* entry  = m_entryModel ? m_entryModel->entryById(runtimeId) : nullptr;
        if (!entry || imageIdx >= entry->images.size()) continue;
        QJsonArray tagArr;
        for (const QString& t : it.value()) tagArr.append(t);
        QJsonObject o;
        o["uuid"]          = entry->uuid;
        o["imageFileName"] = entry->images[imageIdx].fileName;
        o["tags"]          = tagArr;
        pushesArr.append(o);
    }

    QJsonArray deactivatedArr;
    for (const QString& t : m_deactivatedTags)
        deactivatedArr.append(t);

    QJsonArray loraUuidsArr;
    for (const QString& uuid : m_activeLoraUuids) loraUuidsArr.append(uuid);

    QJsonObject root;
    root["activeTags"]      = tagsArr;
    root["tagWeights"]      = weightsObj;
    root["activePushes"]    = pushesArr;
    root["deactivatedTags"] = deactivatedArr;
    root["activeLoraUuids"] = loraUuidsArr;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(root).toJson());
}

void PromptComposerPage::restoreSession(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();

    m_activeTags.clear();
    m_activeTagSet.clear();
    m_tagWeights.clear();
    m_activePushes.clear();
    m_deactivatedTags.clear();

    for (const QJsonValue& v : root["activeTags"].toArray()) {
        const QString t = v.toString();
        if (!t.isEmpty() && !m_activeTagSet.contains(t)) {
            m_activeTags << t;
            m_activeTagSet.insert(t);
        }
    }

    const QJsonObject weightsObj = root["tagWeights"].toObject();
    for (auto it = weightsObj.constBegin(); it != weightsObj.constEnd(); ++it)
        m_tagWeights[it.key()] = float(it.value().toDouble(1.0));

    for (const QJsonValue& v : root["deactivatedTags"].toArray())
        if (const QString t = v.toString(); !t.isEmpty())
            m_deactivatedTags.insert(t);

    for (const QJsonValue& v : root["activePushes"].toArray()) {
        const QJsonObject o        = v.toObject();
        const QString uuid         = o["uuid"].toString();
        const QString imageFileName = o["imageFileName"].toString();
        if (uuid.isEmpty() || imageFileName.isEmpty()) continue;
        core::Entry* entry = m_entryModel ? m_entryModel->entryByUuid(uuid) : nullptr;
        if (!entry) continue;
        int imageIdx = -1;
        for (int i = 0; i < entry->images.size(); ++i)
            if (entry->images[i].fileName == imageFileName) { imageIdx = i; break; }
        if (imageIdx < 0) continue;
        QList<QString> tags;
        for (const QJsonValue& t : o["tags"].toArray())
            tags << t.toString();
        m_activePushes[(qint64(entry->id) << 32) | quint32(imageIdx)] = tags;
    }

    QMap<int, QList<int>> activeGroups;
    for (auto it = m_activePushes.constBegin(); it != m_activePushes.constEnd(); ++it) {
        const int eid = int(quint32(it.key() >> 32));
        const int img = int(quint32(it.key() & 0xFFFFFFFFLL));
        activeGroups[eid].append(img);
    }
    emit activeGroupsChanged(activeGroups);

    m_activeLoraUuids.clear();
    for (const QJsonValue& v : root["activeLoraUuids"].toArray())
        m_activeLoraUuids << v.toString();
    emit loraUuidsRestored(m_activeLoraUuids);

    if (!m_activeTags.isEmpty())
        repush();
}

} // namespace gui
