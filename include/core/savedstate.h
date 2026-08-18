#pragma once
#include <utils/appsettings.h>
#include <QString>
#include <QStringList>
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
    // Facets for composer-injected custom tags (free text that qualifies for
    // a group). Kept with the state, not in tag_definitions.fct.
    QHash<QString, QList<QString>> customTagFacets;
    QMap<QString, bool> ruleStates;              // rule -> enabled
    QMap<QString, QList<QString>> ruleArguments; // rule -> Add/Replace args
    // Full rule definitions at capture time. On restore, any rule here whose
    // name is missing from the local rules.fct is appended (existing-named
    // rules are never overwritten - the local definition wins).
    QJsonArray rulesSnapshot;
    // QList (not QMap) so user-defined variable order survives save/restore.
    QList<QPair<QString, QString>> varValues;
    QString selectedWorkflowId; // WorkflowFile::id
    // Array (not object) so workflow var order survives round-trips.
    QJsonArray workflowVarValues;
    QString previewImagePath;
    QList<QString> activeLoraUuids;

    // Group / format profile stamp. Names are for the UI; the resolved
    // snapshots are what restore applies, so a state still replays its
    // original prompt after the profile is edited or deleted. False on
    // legacy states (no stamp) - restore then leaves the live profiles alone.
    bool profilesStamped = false;
    QString groupProfileName;
    QStringList groupOrder; // resolved: every group name, in order
    QString formatProfileName;
    QList<utils::FacetFormat> facetFormats; // resolved

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
