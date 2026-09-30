#pragma once
#include <QString>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

namespace tc {

struct Settings;

// Bulk in-place edits over a folder of image + .txt sidecar pairs. The edits
// run in a fixed order -- remove named, remove first, prepend, append -- so a
// tag removed here cannot come back through a later step. Logging frequencies
// is read-only. There is no undo: this writes to the source files.
class BatchEditPage : public QWidget {
    Q_OBJECT

public:
    explicit BatchEditPage(Settings& settings, QWidget* parent = nullptr);

    // The handoff from another tab, which has already picked the folder.
    void setInputFolder(const QString& folder);

private:
    void run();
    void persistSettings();

    Settings* m_settings = nullptr;

    QLineEdit* m_folderEdit = nullptr;
    QCheckBox* m_recursive = nullptr;

    QCheckBox* m_removeTag = nullptr;
    QLineEdit* m_removeTagInput = nullptr;
    QCheckBox* m_removeFirst = nullptr;
    QCheckBox* m_prepend = nullptr;
    QLineEdit* m_prependInput = nullptr;
    QCheckBox* m_append = nullptr;
    QLineEdit* m_appendInput = nullptr;
    QCheckBox* m_logFrequencies = nullptr;

    QPushButton* m_runBtn = nullptr;

    QLabel* m_status = nullptr;
    QProgressBar* m_progress = nullptr;
    QPlainTextEdit* m_log = nullptr;
};

} // namespace tc
