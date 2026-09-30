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

// Bulk in-place edits of .txt sidecars: remove named, remove first, prepend,
// append, in that order. No undo; this writes the source files.
class BatchEditPage : public QWidget {
    Q_OBJECT

public:
    explicit BatchEditPage(Settings& settings, QWidget* parent = nullptr);

    // Handoff from another tab.
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
