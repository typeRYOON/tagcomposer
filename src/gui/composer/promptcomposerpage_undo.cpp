// Undo/redo for PromptComposerPage. Snapshots are reuses of core::SavedState
// (the same struct that backs named user states), captured *after* each
// committed user action. The undo stack invariant is "top == current state",
// so undo pops the top, pushes it onto redo, and reapplies the new top.

#include <gui/composer/promptcomposerpage.h>
#include <QDateTime>
#include <QPushButton>

namespace gui {

void PromptComposerPage::captureUndoSnapshot(const QString& kind)
{
    if (m_suppressUndoCapture) return;

    UndoEntry e;
    captureCurrentState(e.snapshot);
    e.kind = kind;
    e.timestamp = QDateTime::currentMSecsSinceEpoch();

    // Coalesce same-kind events within the window so slider/typing spam
    // collapses to one undo step. Empty kind never coalesces.
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
    // Need [baseline, ..., current]; the current-state entry on top is what
    // gets popped. Bottom (baseline) stays so we can keep undoing back to it.
    if (m_undoStack.size() < 2) return;
    UndoEntry top = m_undoStack.takeLast();
    m_redoStack.append(top);
    if (m_redoStack.size() > kUndoStackCap) m_redoStack.removeFirst();

    m_suppressUndoCapture = true;
    restoreState(m_undoStack.last().snapshot);
    m_suppressUndoCapture = false;
    updateUndoButtons();
}

void PromptComposerPage::redo()
{
    if (m_redoStack.isEmpty()) return;
    UndoEntry top = m_redoStack.takeLast();
    m_undoStack.append(top);
    if (m_undoStack.size() > kUndoStackCap) m_undoStack.removeFirst();

    m_suppressUndoCapture = true;
    restoreState(top.snapshot);
    m_suppressUndoCapture = false;
    updateUndoButtons();
}

void PromptComposerPage::updateUndoButtons()
{
    if (m_undoBtn) m_undoBtn->setEnabled(m_undoStack.size() >= 2);
    if (m_redoBtn) m_redoBtn->setEnabled(!m_redoStack.isEmpty());
}

} // namespace gui
