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

namespace utils {
struct AppSettings;
}

namespace gui {

// Bulk in-place edits across a folder of <image>+<image>.txt sidecars.
// Edits apply in fixed order so results are predictable: remove specific
// tag, remove first, prepend, append. "Log tag frequencies" is read-only.
// No undo - operates directly on the source files.
class BatchEditPage : public QWidget {
    Q_OBJECT
public:
    explicit BatchEditPage(utils::AppSettings* settings = nullptr, QWidget* parent = nullptr);

    // Used by the Auto-tagger -> Batch Edit and Tag Editor -> Batch Edit handoffs.
    void setInputFolder(const QString& folder);

private:
    utils::AppSettings* m_settings = nullptr;

    // ---- Left panel (params + ops)
    QLineEdit* m_folderEdit = nullptr;
    QPushButton* m_browseBtn = nullptr;
    QCheckBox* m_recursiveCheck = nullptr;

    QCheckBox* m_removeTagCheck = nullptr;
    QLineEdit* m_removeTagInput = nullptr;
    QCheckBox* m_removeFirstCheck = nullptr;
    QCheckBox* m_prependCheck = nullptr;
    QLineEdit* m_prependInput = nullptr;
    QCheckBox* m_appendCheck = nullptr;
    QLineEdit* m_appendInput = nullptr;
    QCheckBox* m_logFreqCheck = nullptr; // read-only - log only

    QPushButton* m_runBtn = nullptr;

    // ---- Right panel (progress + log)
    QLabel* m_statusLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QPlainTextEdit* m_log = nullptr;

    void onRun();
    void persistSettings();
};

} // namespace gui
