#include <core/composer_store.h>
#include <QDateTime>

using namespace Qt::StringLiterals;

namespace tc {

ComposerStore::Edit::Edit(ComposerStore* store, const QString& kind) : m_store(store)
{
    m_store->pushUndo(kind);
}

ComposerStore::Edit::~Edit()
{
    m_store->notify();
}

ComposerStore::ComposerStore(QObject* parent) : QObject(parent) {}

const ComposerDoc& ComposerStore::doc() const
{
    return m_doc;
}

ComposerStore::Edit ComposerStore::beginEdit(const QString& kind)
{
    return Edit(this, kind);
}

void ComposerStore::pushUndo(const QString& kind)
{
    m_redo.clear();

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!m_undo.isEmpty() && !kind.isEmpty() && m_undo.last().kind == kind
        && now - m_undo.last().at < kCoalesceMs) {
        m_undo.last().at = now;
        return;
    }

    m_undo.append(Snapshot{m_doc, kind, now});
    while (m_undo.size() > kUndoCap)
        m_undo.removeFirst();
}

void ComposerStore::notify()
{
    emit docChanged();
    emit undoStateChanged(canUndo(), canRedo());
}

void ComposerStore::reset(ComposerDoc doc)
{
    m_doc = std::move(doc);
    m_undo.clear();
    m_redo.clear();
    notify();
}

void ComposerStore::addTags(const QStringList& tags)
{
    QStringList toAdd;
    QStringList toReactivate;

    for (const QString& raw : tags) {
        const QString tag = raw.trimmed();
        if (tag.isEmpty()) continue;

        if (m_doc.activeTags.contains(tag)) {
            if (m_doc.deactivated.contains(tag) && !toReactivate.contains(tag))
                toReactivate << tag;
            continue;
        }
        if (!toAdd.contains(tag)) toAdd << tag;
    }

    if (toAdd.isEmpty() && toReactivate.isEmpty()) return;

    const Edit e = beginEdit(u"addTags"_s);
    for (const QString& tag : toReactivate)
        m_doc.deactivated.remove(tag);
    m_doc.activeTags += toAdd;
}

void ComposerStore::eraseTag(const QString& tag)
{
    m_doc.activeTags.removeAll(tag);
    m_doc.deactivated.remove(tag);
    m_doc.weights.remove(tag);
    m_doc.customFacets.remove(tag);
}

void ComposerStore::removeTag(const QString& tag)
{
    if (!m_doc.activeTags.contains(tag)) return;

    const Edit e = beginEdit(u"removeTag"_s);
    eraseTag(tag);
}

bool ComposerStore::dropTags(const QStringList& tags)
{
    bool changed = false;
    for (const QString& tag : tags) {
        if (!m_doc.activeTags.contains(tag)) continue;
        eraseTag(tag);
        changed = true;
    }
    if (changed) notify();
    return changed;
}

bool ComposerStore::renameTag(const QString& from, const QString& to)
{
    const qsizetype at = m_doc.activeTags.indexOf(from);
    if (at < 0 || to.isEmpty() || to == from) return false;

    const bool collides = m_doc.activeTags.contains(to);
    const Edit e = beginEdit(u"rename:"_s + from);

    if (collides) {
        // The target keeps its own weight and facets.
        m_doc.activeTags.removeAt(at);
        m_doc.weights.remove(from);
        m_doc.customFacets.remove(from);
    } else {
        m_doc.activeTags[at] = to;
        if (m_doc.weights.contains(from)) m_doc.weights.insert(to, m_doc.weights.take(from));
        if (m_doc.customFacets.contains(from))
            m_doc.customFacets.insert(to, m_doc.customFacets.take(from));
    }
    m_doc.deactivated.remove(from);

    // Pushes follow the rename; on a collision the old name is just dropped.
    for (EntryPush& push : m_doc.pushes) {
        const qsizetype claimed = push.tags.indexOf(from);
        if (claimed < 0) continue;
        if (collides || push.tags.contains(to))
            push.tags.removeAt(claimed);
        else
            push.tags[claimed] = to;
    }
    return true;
}

void ComposerStore::setDeactivated(const QString& tag, bool on)
{
    if (!m_doc.activeTags.contains(tag)) return;
    if (m_doc.deactivated.contains(tag) == on) return;

    const Edit e = beginEdit(u"deactivate"_s);
    if (on)
        m_doc.deactivated.insert(tag);
    else
        m_doc.deactivated.remove(tag);
}

void ComposerStore::setWeight(const QString& tag, float weight)
{
    const Weight current = weightOf(m_doc, tag);
    if (current.wasSet && qFuzzyCompare(current.value, weight)) return;

    const Edit e = beginEdit(u"weight"_s);
    m_doc.weights.insert(tag, weight);
}

void ComposerStore::clearWeight(const QString& tag)
{
    if (!m_doc.weights.contains(tag)) return;

    const Edit e = beginEdit(u"weight"_s);
    m_doc.weights.remove(tag);
}

void ComposerStore::setCustomFacets(const QString& tag, const QStringList& facets)
{
    if (m_doc.customFacets.value(tag) == facets) return;

    const Edit e = beginEdit(u"customFacets"_s);
    if (facets.isEmpty())
        m_doc.customFacets.remove(tag);
    else
        m_doc.customFacets.insert(tag, facets);
}

bool ComposerStore::isPushed(const QString& entryUuid, const QString& imageFile) const
{
    for (const EntryPush& p : m_doc.pushes)
        if (p.entryUuid == entryUuid && p.imageFile == imageFile) return true;
    return false;
}

void ComposerStore::push(const EntryPush& push, const std::optional<Lora>& lora)
{
    const Edit e = beginEdit(u"push"_s);

    for (qsizetype i = 0; i < m_doc.pushes.size(); ++i) {
        if (m_doc.pushes[i].entryUuid == push.entryUuid
            && m_doc.pushes[i].imageFile == push.imageFile) {
            m_doc.pushes.removeAt(i);
            break;
        }
    }
    m_doc.pushes << push;

    for (const QString& tag : push.tags) {
        if (m_doc.activeTags.contains(tag)) {
            m_doc.deactivated.remove(tag);
            continue;
        }
        m_doc.activeTags << tag;
    }

    if (!lora) return;
    for (const Lora& have : m_doc.loraStack)
        if (have.sha256 == lora->sha256) return;
    m_doc.loraStack << *lora;
}

void ComposerStore::unpush(const QString& entryUuid, const QString& imageFile,
                           const QString& loraSha)
{
    qsizetype at = -1;
    for (qsizetype i = 0; i < m_doc.pushes.size(); ++i) {
        if (m_doc.pushes[i].entryUuid == entryUuid && m_doc.pushes[i].imageFile == imageFile) {
            at = i;
            break;
        }
    }
    if (at < 0) return;

    const Edit e = beginEdit(u"unpush"_s);
    const QStringList orphaned = m_doc.pushes[at].tags;
    m_doc.pushes.removeAt(at);

    for (const QString& tag : orphaned) {
        bool claimed = false;
        for (const EntryPush& p : m_doc.pushes) {
            if (p.tags.contains(tag)) {
                claimed = true;
                break;
            }
        }
        if (claimed) continue;

        m_doc.activeTags.removeAll(tag);
        m_doc.deactivated.remove(tag);
        m_doc.weights.remove(tag);
        m_doc.customFacets.remove(tag);
    }

    // Keep the LoRA while another image of the same entry is still pushed.
    if (loraSha.isEmpty()) return;
    for (const EntryPush& p : m_doc.pushes)
        if (p.entryUuid == entryUuid) return;

    for (qsizetype i = 0; i < m_doc.loraStack.size(); ++i) {
        if (m_doc.loraStack[i].sha256 == loraSha) {
            m_doc.loraStack.removeAt(i);
            break;
        }
    }
}

void ComposerStore::setLoraStack(QList<Lora> stack)
{
    if (m_doc.loraStack == stack) return;

    const Edit e = beginEdit(u"loraStack"_s);
    m_doc.loraStack = std::move(stack);
}

void ComposerStore::clear()
{
    if (m_doc == ComposerDoc{}) return;

    const Edit e = beginEdit(u"clear"_s);
    m_doc = ComposerDoc{};
}

bool ComposerStore::canUndo() const
{
    return !m_undo.isEmpty();
}

bool ComposerStore::canRedo() const
{
    return !m_redo.isEmpty();
}

void ComposerStore::undo()
{
    if (m_undo.isEmpty()) return;

    m_redo.append(Snapshot{m_doc, QString(), 0});
    m_doc = m_undo.takeLast().doc;
    notify();
}

void ComposerStore::redo()
{
    if (m_redo.isEmpty()) return;

    m_undo.append(Snapshot{m_doc, QString(), 0});
    m_doc = m_redo.takeLast().doc;
    notify();
}

} // namespace tc
