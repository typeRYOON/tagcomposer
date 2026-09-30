#pragma once
#include <core/composer_doc.h>
#include <core/prompt.h>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

namespace tc {

// EntryPush itself lives in composer_doc.h, because the live document and a
// saved one describe the same thing.
QJsonObject entryPushToJson(const EntryPush& push);
EntryPush entryPushFromJson(const QJsonObject& obj);

// A whole composer session, saved under data/states/<id>/state.json.
struct SavedState {
    QString id;
    QString name;

    // The sort key, bumped on overwrite, rename and preview drop so the most
    // recently touched state floats to the top. Legacy states have no field,
    // and their id was already a millisecond timestamp.
    qint64 createdAt = 0;

    QStringList activeTags;
    QHash<QString, float> tagWeights;
    QSet<QString> deactivatedTags;
    QHash<QString, QString> deactivatedCategory; // tag -> category when it was turned off
    QList<EntryPush> activePushes;

    // Facets for composer-injected free text. Kept with the state rather than
    // in tag_definitions.fct, because the definition purge drops anything not
    // in Danbooru and unused by an entry.
    QHash<QString, QStringList> customTagFacets;

    QMap<QString, bool> ruleStates;         // uuid -> enabled
    QMap<QString, QStringList> ruleArguments; // uuid -> Add/Replace arguments

    // Every rule as it stood at capture time. On restore, a rule here whose
    // uuid is missing locally is appended; an existing one is never
    // overwritten, so the local definition wins.
    QJsonArray rulesSnapshot;

    // A list, not a map, so the user's variable order survives the trip.
    QList<QPair<QString, QString>> varValues;

    QString selectedWorkflowId;
    QJsonArray workflowVarValues; // an array, so var order survives too

    QString previewImagePath;
    QStringList activeLoraUuids;

    // The group and format profile stamp. The names are for the UI; the
    // resolved snapshots are what a restore applies, so a state still replays
    // its original prompt after its profile is edited or deleted. False on a
    // legacy state, and a restore then leaves the live profiles alone.
    bool profilesStamped = false;
    QString groupProfileName;
    QStringList groupOrder; // resolved: every group name, in order
    QString formatProfileName;
    QList<FacetFormat> facetFormats; // resolved

    static SavedState fromJson(const QJsonObject& obj);
    QJsonObject toJson() const;
};

// One state per subdirectory of the states dir, newest first.
class StateManager {
public:
    static StateManager loadFromDir(const QString& dir);
    void saveToDir(const QString& dir) const;

    QList<SavedState>& states();
    const QList<SavedState>& states() const;

private:
    QList<SavedState> m_states;
};

} // namespace tc
