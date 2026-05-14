#pragma once
#include <QString>
#include <QList>
#include <QHash>
#include <QSet>
#include <QMap>
#include <QJsonArray>
#include <QJsonObject>

namespace core {

struct EntryPush {
    QString uuid;
    QString imageFileName; // stable; never reused after deletion
    QList<QString> tags;

    QJsonObject toJson() const;
    static EntryPush fromJson(const QJsonObject& obj);
};

struct SavedState {
    QString id;
    QString name;
    // Sort key. Bumped on overwrite/rename/preview-drop so most-recently-edited
    // floats to top. Falls back to id-as-timestamp on load when missing.
    qint64 createdAt = 0;
    QList<QString> activeTags;
    QHash<QString, float> tagWeights;
    QSet<QString> deactivatedTags;
    QHash<QString, QString> deactivatedCategory; // tag -> category at deactivation
    QList<EntryPush> activePushes;               // uuid+imageIdx -> tags
    QMap<QString, bool> ruleStates;              // rule -> enabled
    QMap<QString, QList<QString>> ruleArguments; // rule -> Add/Replace args
    // QList (not QMap) so user-defined variable order survives save/restore.
    QList<QPair<QString, QString>> varValues;
    QString selectedWorkflowId; // WorkflowFile::id
    // Array (not object) so workflow var order survives round-trips.
    QJsonArray workflowVarValues;
    QString previewImagePath;
    QList<QString> activeLoraUuids;

    static SavedState fromJson(const QJsonObject& obj);
    QJsonObject toJson() const;
};

class StateManager {
public:
    static StateManager loadFromDir(const QString& dir);
    void saveToDir(const QString& dir) const;

    QList<SavedState>& states()
    {
        return m_states;
    }
    const QList<SavedState>& states() const
    {
        return m_states;
    }

private:
    QList<SavedState> m_states;
};

} // namespace core
