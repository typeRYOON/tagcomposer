#pragma once
#include <QString>
#include <QList>

namespace core {

struct TagGroup {
    QString name;
    QList<QString> facets; // a tag joins the group iff it has all of these
};

class TagGroupIndex {
public:
    static TagGroupIndex loadFromFile(const QString& path);

    // First group whose facets are all present in tagFacets, or "" on no match.
    QString groupFor(const QList<QString>& tagFacets) const;

    const QList<TagGroup>& groups() const;

    // Replaces the group list wholesale. Used by profile ordering, which
    // permutes the groups.fct definitions without redefining them.
    void setGroups(QList<TagGroup> groups);

private:
    QList<TagGroup> m_groups;
};

} // namespace core
