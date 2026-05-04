// PromptComposerPage - session save/restore.
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
        if (qAbs(it.value() - 1.0f) >= 0.001f) weightsObj[it.key()] = double(it.value());

    QJsonArray pushesArr;
    for (const core::EntryPush& ep : dumpActivePushes())
        pushesArr.append(ep.toJson());

    QJsonArray deactivatedArr;
    for (const QString& t : m_deactivatedTags)
        deactivatedArr.append(t);

    QJsonArray loraUuidsArr;
    for (const QString& uuid : m_activeLoraUuids)
        loraUuidsArr.append(uuid);

    QJsonObject root;
    root["activeTags"] = tagsArr;
    root["tagWeights"] = weightsObj;
    root["activePushes"] = pushesArr;
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
    m_deactivatedTags.clear();
    // m_activePushes is replaced wholesale by loadActivePushes below.

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
        if (const QString t = v.toString(); !t.isEmpty()) m_deactivatedTags.insert(t);

    QList<core::EntryPush> pushes;
    for (const QJsonValue& v : root["activePushes"].toArray())
        pushes << core::EntryPush::fromJson(v.toObject());
    loadActivePushes(pushes);

    m_activeLoraUuids.clear();
    for (const QJsonValue& v : root["activeLoraUuids"].toArray())
        m_activeLoraUuids << v.toString();
    emit loraUuidsRestored(m_activeLoraUuids);

    repush();
}

} // namespace gui
