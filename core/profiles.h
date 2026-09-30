#pragma once
#include <core/prompt.h>
#include <core/tag_groups.h>
#include <QList>
#include <QString>
#include <QStringList>

namespace tc {

// A named ordering of the groups groups.fct already defines. It never
// redefines a group's name or facets - groups.fct stays the one definition of
// what a group is. Naming a subset is fine: the groups left out keep their
// file order and follow the named ones.
struct GroupProfile {
    QString name;
    QStringList order;
};

// A named set of per-facet tag wraps: one alternative to settings.json's
// facets.formats list. Independent of GroupProfile.
struct FormatProfile {
    QString name;
    QList<FacetFormat> formats;
};

// `base` with the groups named in `order` hoisted to the front in that order.
// Groups not named follow in base order, and an unknown name is ignored,
// since a group may have been renamed or removed since the profile was
// written.
TagGroups applyGroupOrder(const TagGroups& base, const QStringList& order);

// profiles.fct. Blocks are @groupprofile and @formatprofile, plus one
// `active = <group> | <format>` line.
class ProfileIndex {
public:
    // A missing file yields an empty index, which is not an error.
    static ProfileIndex loadFromFile(const QString& path);

    // One "Default" per axis mirroring current behaviour, so a first run with
    // no profiles.fct behaves exactly as before.
    static ProfileIndex withDefaults(const TagGroups& groups,
                                     const QList<FacetFormat>& formats);

    // A full rewrite, used for the first-run migration. It drops comments.
    void saveToFile(const QString& path) const;

    // Rewrites only the `active` line, so hand-written comments and profile
    // bodies survive a combo-box switch. Falls back to a full write when the
    // file is missing.
    void saveActiveToFile(const QString& path) const;

    bool isEmpty() const;

    const QList<GroupProfile>& groupProfiles() const;
    const QList<FormatProfile>& formatProfiles() const;

    // nullptr when nothing carries that name.
    const GroupProfile* groupProfile(const QString& name) const;
    const FormatProfile* formatProfile(const QString& name) const;

    const QString& activeGroup() const;
    const QString& activeFormat() const;
    void setActiveGroup(const QString& name);
    void setActiveFormat(const QString& name);

    // The resolved active payloads. Empty when the active name is unset or
    // names a profile that no longer exists; the caller then keeps base order
    // and the settings.json format list.
    QStringList activeOrder() const;
    QList<FacetFormat> activeFormats() const;

private:
    QList<GroupProfile> m_groupProfiles;
    QList<FormatProfile> m_formatProfiles;
    QString m_activeGroup;
    QString m_activeFormat;
};

} // namespace tc
