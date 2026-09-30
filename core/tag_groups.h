#pragma once
#include <core/load_error.h>
#include <QList>
#include <QString>
#include <QStringList>
#include <expected>

namespace tc {

// A tag joins when it has all of these facets.
struct TagGroup {
    QString name;
    QStringList facets;

    bool operator==(const TagGroup&) const = default;
};

// groups.fct: @group blocks, first match wins, so order matters.
class TagGroups {
public:
    // Name of the first group whose facets are all present, or empty.
    QString groupFor(const QStringList& tagFacets) const;

    const QList<TagGroup>& all() const;
    void setAll(QList<TagGroup> groups);
    QStringList names() const;

private:
    QList<TagGroup> m_groups;
};

struct TagGroupsFile {
    TagGroups groups;
    QStringList header;
    QList<LoadError> warnings;
    QString eol = QStringLiteral("\n");
    bool trailingNewline = true;
};

std::expected<TagGroupsFile, LoadError> readTagGroups(const QString& path);
std::expected<void, LoadError> writeTagGroups(const TagGroupsFile& file, const QString& path);

// A precedes B and A's facets are a subset of B's, so B can never match.
struct GroupShadow {
    QString shadower;
    QString shadowed;
};

QList<GroupShadow> shadowedGroups(const TagGroups& groups);

} // namespace tc
