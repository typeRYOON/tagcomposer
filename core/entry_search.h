#pragma once
#include <core/entry_store.h>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <optional>

namespace tc {

struct NumFilter {
    enum class Op { Off, Eq, Lt, Le, Gt, Ge };

    Op op = Op::Off;
    int value = 0;

    bool active() const;
    bool match(int n) const;

    bool operator==(const NumFilter&) const = default;
};

// A term ending in ']' matches the whole tag; otherwise it is a prefix.
struct TagTerm {
    QString text;
    bool exact = false;

    bool operator==(const TagTerm&) const = default;
};

// A comma-separated AND group.
struct QueryGroup {
    QList<TagTerm> tags;
    QList<TagTerm> excluded;
    QString title;
    QString comment;
    QString lora;
    NumFilter imageCount;
    NumFilter tagCount;
    std::optional<bool> hasLora;
    std::optional<bool> hasTitle;
    std::optional<bool> hasComment;

    bool operator==(const QueryGroup&) const = default;
};

// Groups are OR'd. sort: is global; the last one wins.
struct EntryQuery {
    QList<QueryGroup> groups;
    QString sortKey;
    QString sortDir;

    bool operator==(const EntryQuery&) const = default;
};

// Unparseable clauses are dropped.
EntryQuery parseEntryQuery(const QString& text);

void sortEntries(QList<const Entry*>& entries, const QString& sortKey, const QString& sortDir);

// Inverted tag index, marked dirty on store changes and rebuilt on the next query.
class EntrySearch : public QObject {
    Q_OBJECT

public:
    explicit EntrySearch(const EntryStore& store, QObject* parent = nullptr);

    QStringList find(const QString& query) const;
    QStringList find(const EntryQuery& query) const;
    void invalidate();
    qsizetype indexedTags() const;
    bool dirty() const;

private:
    void rebuild() const;
    QList<qsizetype> candidates(const QueryGroup& group) const;
    QList<qsizetype> forTerm(const TagTerm& term) const;

    const EntryStore* m_store = nullptr;
    mutable bool m_dirty = true;
    mutable QStringList m_sortedTags;
    mutable QHash<QString, QList<qsizetype>> m_postings;
};

} // namespace tc
