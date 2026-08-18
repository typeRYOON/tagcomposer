// Undo/redo. Snapshots reuse core::SavedState, captured *after* each
// committed action. Invariant: m_undoStack.last() == current state.

#include <gui/composer/promptcomposerpage.h>
#include <QDateTime>
#include <QPushButton>

namespace {

QString nameOne(const QString& verb, const QList<QString>& items)
{
    if (items.size() == 1) return QString("%1 \"%2\"").arg(verb, items.first());
    return QString("%1 %2 tags").arg(verb).arg(items.size());
}

// Short summary of what differs between two snapshots, so undo/redo can say
// what they actually did. Derived from the states themselves rather than a
// label at each capture site - the labels would also drive coalescing, and
// most sites pass none. Ordered most-specific first.
QString describeChange(const core::SavedState& from, const core::SavedState& to,
                       const core::RuleEngine* rules)
{
    const QSet<QString> fromTags(from.activeTags.cbegin(), from.activeTags.cend());
    const QSet<QString> toTags(to.activeTags.cbegin(), to.activeTags.cend());
    const QList<QString> added = (toTags - fromTags).values();
    const QList<QString> removed = (fromTags - toTags).values();
    if (!added.isEmpty() && removed.isEmpty()) return nameOne("added", added);
    if (!removed.isEmpty() && added.isEmpty()) return nameOne("removed", removed);
    if (!added.isEmpty())
        return QString("replaced %1 tag(s)").arg(qMax(added.size(), removed.size()));

    const QList<QString> deact = (to.deactivatedTags - from.deactivatedTags).values();
    const QList<QString> react = (from.deactivatedTags - to.deactivatedTags).values();
    if (!deact.isEmpty()) return nameOne("deactivated", deact);
    if (!react.isEmpty()) return nameOne("reactivated", react);

    {
        QSet<QString> keys(from.tagWeights.keyBegin(), from.tagWeights.keyEnd());
        for (auto it = to.tagWeights.keyBegin(); it != to.tagWeights.keyEnd(); ++it)
            keys.insert(*it);
        QList<QString> changed;
        for (const QString& k : keys)
            if (!qFuzzyCompare(from.tagWeights.value(k, 1.0f), to.tagWeights.value(k, 1.0f)))
                changed << k;
        if (changed.size() == 1) {
            return QString("weight on \"%1\" (%2)")
                .arg(changed.first(),
                     QString::number(double(to.tagWeights.value(changed.first(), 1.0f)), 'f', 2));
        }
        if (!changed.isEmpty()) return QString("%1 weight changes").arg(changed.size());
    }

    {
        QSet<QString> uuids(from.ruleStates.keyBegin(), from.ruleStates.keyEnd());
        for (auto it = to.ruleStates.keyBegin(); it != to.ruleStates.keyEnd(); ++it)
            uuids.insert(*it);
        QList<QString> toggled;
        for (const QString& u : uuids)
            if (from.ruleStates.value(u, false) != to.ruleStates.value(u, false)) toggled << u;
        if (toggled.size() == 1) {
            const QString uuid = toggled.first();
            QString name = QStringLiteral("rule");
            if (rules)
                for (const core::Rule& r : rules->rules())
                    if (r.uuid == uuid) name = r.name;
            return QString("%1 rule \"%2\"")
                .arg(to.ruleStates.value(uuid, false) ? "enabled" : "disabled", name);
        }
        if (!toggled.isEmpty()) return QString("%1 rule toggles").arg(toggled.size());
        if (from.ruleArguments != to.ruleArguments) return QStringLiteral("rule arguments");
    }

    if (from.varValues != to.varValues) return QStringLiteral("variables");
    if (from.selectedWorkflowId != to.selectedWorkflowId)
        return QStringLiteral("workflow selection");
    if (from.workflowVarValues != to.workflowVarValues) return QStringLiteral("workflow variables");
    if (from.groupProfileName != to.groupProfileName || from.groupOrder != to.groupOrder) {
        return to.groupProfileName.isEmpty()
                   ? QStringLiteral("group order")
                   : QString("group profile \"%1\"").arg(to.groupProfileName);
    }
    if (from.formatProfileName != to.formatProfileName || from.facetFormats != to.facetFormats) {
        return to.formatProfileName.isEmpty()
                   ? QStringLiteral("tag formatting")
                   : QString("format profile \"%1\"").arg(to.formatProfileName);
    }
    if (from.activeLoraUuids != to.activeLoraUuids) return QStringLiteral("LoRA stack");
    if (from.activePushes.size() != to.activePushes.size()) {
        return to.activePushes.size() > from.activePushes.size()
                   ? QStringLiteral("entry push")
                   : QStringLiteral("entry un-push");
    }
    return QStringLiteral("changes");
}

} // namespace

namespace gui {

void PromptComposerPage::captureUndoSnapshot(const QString& kind)
{
    if (m_suppressUndoCapture) return;

    UndoEntry e;
    captureCurrentState(e.snapshot);
    e.kind = kind;
    e.timestamp = QDateTime::currentMSecsSinceEpoch();

    // Same-kind within the window replaces the top (slider/typing spam).
    // Empty kind never coalesces.
    if (!m_undoStack.isEmpty() && !kind.isEmpty() && m_undoStack.last().kind == kind &&
        (e.timestamp - m_undoStack.last().timestamp) < kUndoCoalesceMs) {
        m_undoStack.last() = e;
    }
    else {
        m_undoStack.append(e);
        if (m_undoStack.size() > kUndoStackCap) m_undoStack.removeFirst();
    }
    m_redoStack.clear();
    updateUndoButtons();
}

void PromptComposerPage::rebaselineUndo()
{
    m_undoStack.clear();
    m_redoStack.clear();
    UndoEntry baseline;
    captureCurrentState(baseline.snapshot);
    baseline.timestamp = QDateTime::currentMSecsSinceEpoch();
    m_undoStack.append(baseline);
    updateUndoButtons();
}

void PromptComposerPage::clearRedoStack()
{
    if (m_redoStack.isEmpty()) return;
    m_redoStack.clear();
    updateUndoButtons();
}

void PromptComposerPage::undo()
{
    // Stack must be [baseline, ..., current]; pop current, baseline stays.
    if (m_undoStack.size() < 2) {
        emit statusMessageRequested("Nothing to undo.");
        return;
    }
    UndoEntry top = m_undoStack.takeLast();
    m_redoStack.append(top);
    if (m_redoStack.size() > kUndoStackCap) m_redoStack.removeFirst();

    m_suppressUndoCapture = true;
    restoreState(m_undoStack.last().snapshot);
    m_suppressUndoCapture = false;
    updateUndoButtons();
    // restoreState stays silent under m_suppressUndoCapture, so this is the
    // only status line for the action.
    emit statusMessageRequested(QString("Undo: %1").arg(
        describeChange(m_undoStack.last().snapshot, top.snapshot, m_rules)));
}

void PromptComposerPage::redo()
{
    if (m_redoStack.isEmpty()) {
        emit statusMessageRequested("Nothing to redo.");
        return;
    }
    UndoEntry top = m_redoStack.takeLast();
    const core::SavedState before =
        m_undoStack.isEmpty() ? core::SavedState() : m_undoStack.last().snapshot;
    m_undoStack.append(top);
    if (m_undoStack.size() > kUndoStackCap) m_undoStack.removeFirst();

    m_suppressUndoCapture = true;
    restoreState(top.snapshot);
    m_suppressUndoCapture = false;
    updateUndoButtons();
    emit statusMessageRequested(
        QString("Redo: %1").arg(describeChange(before, top.snapshot, m_rules)));
}

void PromptComposerPage::updateUndoButtons()
{
    if (m_undoBtn) m_undoBtn->setEnabled(m_undoStack.size() >= 2);
    if (m_redoBtn) m_redoBtn->setEnabled(!m_redoStack.isEmpty());
}

} // namespace gui
