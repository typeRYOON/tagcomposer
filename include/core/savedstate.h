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
    QString        uuid;
    QString        imageFileName; // stable; never reused after deletion
    QList<QString> tags;

    QJsonObject     toJson() const;
    static EntryPush fromJson(const QJsonObject& obj);
};

struct SavedState {
    QString id;
    QString name;
    QList<QString>         activeTags;
    QHash<QString, float>  tagWeights;
    QSet<QString>          deactivatedTags;
    QList<EntryPush>       activePushes;     // uuid + imageIdx -> tags (stable across restarts)
    QMap<QString, bool>           ruleStates;     // rule name -> enabled
    QMap<QString, QList<QString>> ruleArguments;  // rule name -> action arguments (for Add/Replace)
    QMap<QString, QString>        varValues;      // var name -> value
    QString                selectedWorkflowId;   // stable id from WorkflowFile::id
    // Array of {placeholder, type, value-fields...} - array (not object) so the
    // workflow's variable order is preserved across save/restore round-trips.
    QJsonArray             workflowVarValues;
    QString                previewImagePath;
    QList<QString>         activeLoraUuids;      // ordered entry UUIDs with active LoRA

    static SavedState fromJson(const QJsonObject& obj);
    QJsonObject toJson() const;
};

class StateManager {
public:
    static StateManager loadFromDir(const QString& dir);
    void saveToDir(const QString& dir) const;

    QList<SavedState>&       states()       { return m_states; }
    const QList<SavedState>& states() const { return m_states; }

private:
    QList<SavedState> m_states;
};

} // namespace core
