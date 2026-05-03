#pragma once
#include <QWidget>
#include <QString>
#include <QList>

class QLineEdit;
class QPushButton;
class QCheckBox;
class QPlainTextEdit;
class QProgressBar;
class QLabel;

namespace utils { struct AppSettings; }

namespace gui {

// Bulk operations across a folder of <image>+<image>.txt pairs. Reads every
// .txt sidecar, applies a configurable sequence of edits, and writes them
// back. Each operation is independent — toggle the ones you want, click Run.
//
// Edit operations (applied in this order, so results are predictable):
//   1. Remove specific tag (by name)
//   2. Remove first tag
//   3. Prepend tag
//   4. Append tag
//
// Read-only operation:
//   * "Log tag frequencies" — does not modify any file. Counts every tag
//     across the folder and writes the totals to the log pane (descending
//     by count). Useful for spotting outliers / under-tagged classes.
//
// All edits are in-place rewrites of the existing .txt files. There's no
// undo — point this at a folder you've already backed up if it matters.
class BatchEditPage : public QWidget {
    Q_OBJECT
public:
    explicit BatchEditPage(utils::AppSettings* settings = nullptr,
                           QWidget*            parent   = nullptr);

    // Used by the Auto-tagger / Tag Editor → Batch Edit handoff.
    void setInputFolder(const QString& folder);

private:
    utils::AppSettings* m_settings = nullptr;

    // ── Left panel (params + ops) ───────────────────────────────────────────
    QLineEdit*    m_folderEdit       = nullptr;
    QPushButton*  m_browseBtn        = nullptr;
    QCheckBox*    m_recursiveCheck   = nullptr;

    QCheckBox*    m_removeTagCheck   = nullptr;
    QLineEdit*    m_removeTagInput   = nullptr;
    QCheckBox*    m_removeFirstCheck = nullptr;
    QCheckBox*    m_prependCheck     = nullptr;
    QLineEdit*    m_prependInput     = nullptr;
    QCheckBox*    m_appendCheck      = nullptr;
    QLineEdit*    m_appendInput      = nullptr;
    QCheckBox*    m_logFreqCheck     = nullptr;  // read-only — log only

    QPushButton*  m_runBtn           = nullptr;

    // ── Right panel (progress + log) ────────────────────────────────────────
    QLabel*         m_statusLabel    = nullptr;
    QProgressBar*   m_progressBar    = nullptr;
    QPlainTextEdit* m_log            = nullptr;

    void onRun();
    void persistSettings();
};

} // namespace gui
