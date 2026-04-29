#pragma once
#include <QString>
#include <QList>

namespace core {

struct TagGroup {
    QString        name;
    QList<QString> facets; // tag goes in this group if it has ALL of these facets
};

class TagGroupIndex {
public:
    static TagGroupIndex loadFromFile(const QString& path);

    // Returns the name of the first group whose facets are ALL present in tagFacets.
    // Returns "" if no group matches.
    QString groupFor(const QList<QString>& tagFacets) const;

    const QList<TagGroup>& groups() const;

private:
    QList<TagGroup> m_groups;
};

} // namespace core
