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

QJsonObject entryPushToJson(const EntryPush& push);
EntryPush entryPushFromJson(const QJsonObject& obj);

// A composer session, saved as data/states/<id>/state.json.
struct SavedState {
    QString id;
    QString name;

    // Sort key; bumped on overwrite, rename and preview drop.
    qint64 createdAt = 0;

    QStringList activeTags;
    QHash<QString, float> tagWeights;
    QSet<QString> deactivatedTags;
    QHash<QString, QString> deactivatedCategory; // tag -> category when it was turned off
    QList<EntryPush> activePushes;

    // Facets for free-text tags. Kept here because the definitions purge drops
    // tags that aren't in Danbooru or any entry.
    QHash<QString, QStringList> customTagFacets;

    QMap<QString, bool> ruleStates;         // uuid -> enabled
    QMap<QString, QStringList> ruleArguments; // uuid -> Add/Replace arguments

    // Rules at capture time. Restore appends ones missing locally; local wins.
    QJsonArray rulesSnapshot;

    // A list to keep variable order.
    QList<QPair<QString, QString>> varValues;

    QString selectedWorkflowId;
    QJsonArray workflowVarValues;

    QString previewImagePath;
    QStringList activeLoraUuids;

    // Profile stamp. Restore applies the resolved snapshots, not the names, so
    // later profile edits don't change old states. Legacy states have none.
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
