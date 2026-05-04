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
    QList<QString> activeTags;
    QHash<QString, float> tagWeights;
    QSet<QString> deactivatedTags;
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
