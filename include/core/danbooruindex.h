#pragma once
#include <QString>
#include <QStringList>
#include <QList>

namespace core {

struct TagSearchResult {
    QString displayName;  // alias name if alias matched, else canonical tag
    QString canonicalTag; // tag to commit (always canonical)
    int category;
    int64_t count;
    bool isAlias;   // matched on an alias; canonicalTag differs from displayName
    int matchStart; // bold-range start in displayName (1 if leading '/')
    int matchLen;
};

class DanbooruIndex {
public:
    static DanbooruIndex* loadFromFile(const QString& path);
    QList<TagSearchResult> search(const QString& prefix, int maxResults = 12) const;
    // tagCategory/tagCount expect the canonical csv form (lowercase, spaces) -
    // callers holding Danbooru wire forms ("long_hair") must normalizeTagInput
    // first. search() normalizes its argument internally.
    int tagCategory(const QString& tag) const; // -1 if not found
    int64_t tagCount(const QString& tag) const; // -1 if not found

private:
    DanbooruIndex() = default;

    struct Tag {
        QString name;
        int category{0};
        int64_t count{0};
        QStringList aliases;
    };

    struct AliasEntry {
        QString normalized; // leading '/' stripped, for sorted lookup
        QString original;   // shown as-is in the search list
        int tagIdx{0};
    };

    struct WordEntry {
        QString word;  // non-leading space-separated word, for sorted lookup
        int tagIdx{0};
        int offset{0}; // byte offset of word within m_tags[tagIdx].name
    };

    QList<Tag> m_tags;
    QList<int> m_byName;         // indices into m_tags, sorted by tag name
    QList<AliasEntry> m_byAlias; // sorted by normalized
    QList<WordEntry> m_byWord;   // sorted by word; non-leading words only
};

} // namespace core
