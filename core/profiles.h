#pragma once
#include <core/prompt.h>
#include <core/tag_groups.h>
#include <QList>
#include <QString>
#include <QStringList>

namespace tc {

// A named ordering of groups.fct's groups. Unnamed groups follow in file order.
struct GroupProfile {
    QString name;
    QStringList order;
};

// A named alternative to settings.json's facet format list.
struct FormatProfile {
    QString name;
    QList<FacetFormat> formats;
};

// Named groups first, in order; the rest keep base order. Unknown names are ignored.
TagGroups applyGroupOrder(const TagGroups& base, const QStringList& order);

// profiles.fct: @groupprofile / @formatprofile blocks, `active = <group> | <format>`.
class ProfileIndex {
public:
    // A missing file yields an empty index.
    static ProfileIndex loadFromFile(const QString& path);

    // One "Default" profile per axis, matching the current settings.
    static ProfileIndex withDefaults(const TagGroups& groups,
                                     const QList<FacetFormat>& formats);

    // Full rewrite; drops comments.
    void saveToFile(const QString& path) const;

    // Rewrites only the active line, keeping comments. Full write if the file is missing.
    void saveActiveToFile(const QString& path) const;

    bool isEmpty() const;

    const QList<GroupProfile>& groupProfiles() const;
    const QList<FormatProfile>& formatProfiles() const;

    // nullptr if not found.
    const GroupProfile* groupProfile(const QString& name) const;
    const FormatProfile* formatProfile(const QString& name) const;

    const QString& activeGroup() const;
    const QString& activeFormat() const;
    void setActiveGroup(const QString& name);
    void setActiveFormat(const QString& name);

    // Empty when unset or the profile is gone; callers then use the defaults.
    QStringList activeOrder() const;
    QList<FacetFormat> activeFormats() const;

private:
    QList<GroupProfile> m_groupProfiles;
    QList<FormatProfile> m_formatProfiles;
    QString m_activeGroup;
    QString m_activeFormat;
};

} // namespace tc
