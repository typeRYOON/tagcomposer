#pragma once
#include <core/taggroups.h>
#include <utils/appsettings.h>
#include <QList>
#include <QString>
#include <QStringList>

namespace core {

// A named ordering of the groups already defined in groups.fct. It never
// redefines a group's name or facets - groups.fct stays the single definition
// of what a group is. May name a subset; groups it omits keep their groups.fct
// order and follow the named ones.
struct GroupProfile {
    QString name;
    QStringList order;
};

// A named set of per-facet tag wraps, i.e. one alternative to the
// settings.json facets.formats list. Independent of GroupProfile.
struct FormatProfile {
    QString name;
    QList<utils::FacetFormat> formats;
};

// Copy of `base` with the groups named in `order` hoisted to the front in that
// order; groups not named follow in base order. Unknown names are ignored (a
// group may have been renamed or removed since the profile was written).
TagGroupIndex applyGroupOrder(const TagGroupIndex& base, const QStringList& order);

// Group names in index order - the resolved snapshot a state stores.
QStringList groupNames(const TagGroupIndex& index);

// "A shadows B" for every pair where A precedes B and A's facets are a subset
// of B's: groupFor is first-match-wins, so every tag that would land in B is
// claimed by A instead. Empty when the ordering is safe.
QStringList shadowedGroups(const TagGroupIndex& ordered);

class ProfileIndex {
public:
    static ProfileIndex loadFromFile(const QString& path);

    // Full rewrite. Used for the first-run migration write; drops comments.
    void saveToFile(const QString& path) const;
    // Rewrites only the "active" line so hand-written comments and profile
    // bodies survive a combo-box switch. Falls back to a full write when the
    // file is missing.
    void saveActiveToFile(const QString& path) const;

    // One "Default" profile per axis mirroring the current behaviour, so a
    // first run with no profiles.fct stays byte-identical.
    static ProfileIndex withDefaults(const TagGroupIndex& groups,
                                     const QList<utils::FacetFormat>& formats);

    bool isEmpty() const
    {
        return m_groupProfiles.isEmpty() && m_formatProfiles.isEmpty();
    }

    const QList<GroupProfile>& groupProfiles() const
    {
        return m_groupProfiles;
    }
    const QList<FormatProfile>& formatProfiles() const
    {
        return m_formatProfiles;
    }

    // nullptr when no profile carries that name.
    const GroupProfile* groupProfile(const QString& name) const;
    const FormatProfile* formatProfile(const QString& name) const;

    QString activeGroup() const
    {
        return m_activeGroup;
    }
    QString activeFormat() const
    {
        return m_activeFormat;
    }
    void setActiveGroup(const QString& name)
    {
        m_activeGroup = name;
    }
    void setActiveFormat(const QString& name)
    {
        m_activeFormat = name;
    }

    // Resolved active payloads. Empty when the active name is unset or names a
    // profile that no longer exists - callers then keep base order / the
    // settings.json format list.
    QStringList activeOrder() const;
    QList<utils::FacetFormat> activeFormats() const;

private:
    QList<GroupProfile> m_groupProfiles;
    QList<FormatProfile> m_formatProfiles;
    QString m_activeGroup;
    QString m_activeFormat;
};

} // namespace core
