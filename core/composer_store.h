#pragma once
#include <core/composer_doc.h>
#include <QObject>
#include <QString>
#include <QStringList>
#include <optional>

namespace tc {

// Owns the composer document and is the only thing that mutates it. Every
// mutator opens an Edit guard, which snapshots for undo on the way in and
// emits on the way out, so a new mutator cannot forget either half.
class ComposerStore : public QObject {
    Q_OBJECT

public:
    explicit ComposerStore(QObject* parent = nullptr);

    const ComposerDoc& doc() const;

    // Replaces the document wholesale and drops both undo stacks.
    void reset(ComposerDoc doc);

    // Already-active tags are skipped; an active but deactivated one is reactivated.
    void addTags(const QStringList& tags);
    void removeTag(const QString& tag);

    // Keeps the tag's place in the list and carries its weight, custom facets
    // and push claims across. When `to` is already active the rename collapses
    // into it instead of duplicating: `from` is dropped and the survivor keeps
    // its own weight. Returns false when nothing changed.
    bool renameTag(const QString& from, const QString& to);

    void setDeactivated(const QString& tag, bool on);

    void setWeight(const QString& tag, float weight);
    void clearWeight(const QString& tag);
    void setCustomFacets(const QString& tag, const QStringList& facets);

    // A push can carry the source entry's LoRA, which then rides with it:
    // pushing activates it, unpushing drops it once no other image of that
    // same entry is still pushed. Both happen inside the push's own edit, so
    // one undo takes the tags and the LoRA back together.
    void push(const EntryPush& push, const std::optional<Lora>& lora = std::nullopt);
    void unpush(const QString& entryUuid, const QString& imageFile,
                const QString& loraSha = QString());
    bool isPushed(const QString& entryUuid, const QString& imageFile) const;

    void setLoraStack(QList<Lora> stack);
    void clear();

    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();

signals:
    void docChanged();
    void undoStateChanged(bool canUndo, bool canRedo);

private:
    // Snapshots on construction and notifies on destruction.
    // Callers hold it by value via guaranteed elision.
    class Edit {
    public:
        Edit(ComposerStore* store, const QString& kind);
        ~Edit();
        Edit(const Edit&) = delete;
        Edit& operator=(const Edit&) = delete;

    private:
        ComposerStore* m_store;
    };

    Edit beginEdit(const QString& kind);
    void pushUndo(const QString& kind);
    void notify();

    struct Snapshot {
        ComposerDoc doc;
        QString kind;
        qint64 at = 0;
    };

    ComposerDoc m_doc;
    QList<Snapshot> m_undo;
    QList<Snapshot> m_redo;

    // A burst of the same edit kind inside this window keeps the one snapshot
    // taken before the burst began, so undoing a slider drag lands before it
    // rather than one step into it.
    static constexpr qint64 kCoalesceMs = 800;
    static constexpr qsizetype kUndoCap = 50;
};

} // namespace tc
