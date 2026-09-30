#pragma once
#include <core/composer_doc.h>
#include <QObject>
#include <QString>
#include <QStringList>
#include <optional>

namespace tc {

// Owns and mutates the composer document. Every mutator opens an Edit guard,
// which snapshots for undo and emits docChanged.
class ComposerStore : public QObject {
    Q_OBJECT

public:
    explicit ComposerStore(QObject* parent = nullptr);

    const ComposerDoc& doc() const;

    // Replaces the document and clears undo/redo.
    void reset(ComposerDoc doc);

    // Active tags are skipped; deactivated ones are reactivated.
    void addTags(const QStringList& tags);
    void removeTag(const QString& tag);

    // Keeps position, weight, facets and pushes. If `to` is already active,
    // `from` merges into it. Returns false if nothing changed.
    bool renameTag(const QString& from, const QString& to);

    void setDeactivated(const QString& tag, bool on);

    void setWeight(const QString& tag, float weight);
    void clearWeight(const QString& tag);
    void setCustomFacets(const QString& tag, const QStringList& facets);

    // The entry's LoRA rides with a push: activated on push, dropped when no
    // image of that entry is still pushed. One undo reverts both.
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
    // RAII: snapshots on construction, notifies on destruction.
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

    // Same-kind edits within this window share one undo step (e.g. a slider drag).
    static constexpr qint64 kCoalesceMs = 800;
    static constexpr qsizetype kUndoCap = 50;
};

} // namespace tc
