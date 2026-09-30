#pragma once
#include <QList>
#include <QString>
#include <QStringList>
#include <QtTypes>

namespace tc {

struct TagSearchResult {
    QString displayName;  // alias if one matched
    QString canonicalTag;
    int category = 0;
    qint64 count = 0;
    bool isAlias = false;
    int matchStart = 0; // bold range in displayName
    int matchLength = 0;
};

// danbooru.csv, sorted for prefix search. Lookups take the normalized form
// ("long hair"); search() normalizes its input itself.
class DanbooruIndex {
public:
    // The csv is optional; a missing file leaves the index empty.
    bool load(const QString& path);
    bool isEmpty() const;

    // Ranked by tier (name prefix, later-word prefix, alias), then post count.
    QList<TagSearchResult> search(const QString& prefix, int maxResults = 12) const;

    int tagCategory(const QString& tag) const; // -1 when unknown
    qint64 tagCount(const QString& tag) const; // -1 when unknown

private:
    struct Tag {
        QString name;
        int category = 0;
        qint64 count = 0;
        QStringList aliases;
    };

    struct AliasEntry {
        QString normalized; // leading '/' stripped
        QString original;
        qsizetype tagIndex = 0;
    };

    struct WordEntry {
        QString word; // a non-leading word of a tag name
        qsizetype tagIndex = 0;
        int offset = 0; // word start within the name
    };

    qsizetype indexOfName(const QString& tag) const;

    QList<Tag> m_tags;
    QList<qsizetype> m_byName; // indices into m_tags, sorted by name
    QList<AliasEntry> m_byAlias;
    QList<WordEntry> m_byWord;
};

} // namespace tc
