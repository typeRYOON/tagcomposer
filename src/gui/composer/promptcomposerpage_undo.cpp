// Undo/redo. Snapshots reuse core::SavedState, captured *after* each
// committed action. Invariant: m_undoStack.last() == current state.

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
