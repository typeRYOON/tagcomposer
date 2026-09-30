#pragma once
#include <QList>
#include <QString>
#include <QStringList>
#include <QtTypes>

namespace tc {

struct TagSearchResult {
    QString displayName;  // the alias when one matched, else the canonical tag
    QString canonicalTag; // what actually gets committed
    int category = 0;
    qint64 count = 0;
    bool isAlias = false;
    int matchStart = 0; // the bold range inside displayName
    int matchLength = 0;
};

// danbooru.csv, read once and held sorted for prefix search.
//
// Lookups take the canonical csv form (lower case, spaces). A caller holding
// a wire form ("long_hair") normalises first; search() does that itself.
class DanbooruIndex {
public:
    // A missing or unreadable file leaves the index empty, which is not an
    // error: the csv is optional and only powers autocomplete.
    bool load(const QString& path);
    bool isEmpty() const;

    // Ranked: name prefix first, then a prefix of any later word, then an
    // alias. Within a tier, the higher post count wins.
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
        QString normalized; // a leading '/' stripped, for the sorted lookup
        QString original;   // shown as written
        qsizetype tagIndex = 0;
    };

    struct WordEntry {
        QString word; // a non-leading word of a tag name
        qsizetype tagIndex = 0;
        int offset = 0; // where that word starts inside the name
    };

    qsizetype indexOfName(const QString& tag) const;

    QList<Tag> m_tags;
    QList<qsizetype> m_byName; // indices into m_tags, sorted by name
    QList<AliasEntry> m_byAlias;
    QList<WordEntry> m_byWord;
};

} // namespace tc
