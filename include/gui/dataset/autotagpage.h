#pragma once
#include <core/autotaggermodel.h>
#include <QWidget>
#include <QHash>
#include <QPixmap>
#include <QSpinBox>

class QComboBox;
class QLineEdit;
class QSlider;
class QCheckBox;
class QPushButton;
class QLabel;
class QProgressBar;
class QListWidget;
class QListWidgetItem;
class QVBoxLayout;

namespace utils {
struct AppSettings;
}
namespace core {
class AutoTaggerLibrary;
class BatchTagger;
} // namespace core

namespace gui {

// Batch autotagging UI. Reads input + output folders, threshold, and the
// active model from AppSettings; writes any changes back. Inference runs on
// a worker thread inside core::BatchTagger; this page just drives the
// surface and renders per-image results.
class AutoTagPage : public QWidget {
    Q_OBJECT
public:
    AutoTagPage(core::AutoTaggerLibrary* library, utils::AppSettings* settings,
                QWidget* parent = nullptr);
    ~AutoTagPage() override;

    // Re-reads the library's available models - called by the parent when
    // the user adds/removes model dirs externally.
    void refreshModels();

    // Used by the inter-tab handoff (Collector → Auto-tagger). Sets the
    // input folder field and persists the value to settings.
    void setInputFolder(const QString& folder);

protected:
    // Image preview opens its source file on left-click.
    bool eventFilter(QObject* obj, QEvent* ev) override;
    // Rescan the library every time the page is shown so deleted/added
    // model folders are reflected.
    void showEvent(QShowEvent* ev) override;

signals:
    // Emitted when the user clicks "Send to Tag Editor". DatasetHelpersPage
    // listens, calls TagEditorPage::setInputFolder, and switches tabs.
    void editFolderRequested(const QString& folder);

    // "Send to Batch Edit" - same handoff pattern, target is BatchEditPage.
    void sendToBatchEditRequested(const QString& folder);

private:
    core::AutoTaggerLibrary* m_library = nullptr;
    utils::AppSettings* m_settings = nullptr;
    core::BatchTagger* m_runner = nullptr;

    // ── Left panel (params) ──────────────────────────────────────────────────
    QComboBox* m_modelBox = nullptr;
    QLineEdit* m_inputEdit = nullptr;
    QPushButton* m_inputBrowseBtn = nullptr;
    QLineEdit* m_outputEdit = nullptr;
    QPushButton* m_outBrowseBtn = nullptr;
    QSlider* m_thresholdSlider = nullptr;
    QLabel* m_thresholdLbl = nullptr;
    QSpinBox* m_cooldownSpin = nullptr;
    QCheckBox* m_recursiveCheck = nullptr;
    QCheckBox* m_moveCheck = nullptr;
    QPushButton* m_runBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QPushButton* m_sendToEditorBtn = nullptr;
    QPushButton* m_sendToBatchBtn = nullptr;

    // ── Middle panel (status + results) ──────────────────────────────────────
    QLabel* m_statusLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QListWidget* m_resultsList = nullptr;
    QWidget* m_emptyState = nullptr;
    QLabel* m_emptyStateLbl = nullptr;

    // ── Right panel (focused result) ─────────────────────────────────────────
    QLabel* m_focusImage = nullptr;
    QLabel* m_focusRating = nullptr;
    QListWidget* m_focusTags = nullptr;
    QString m_focusedRel;

    // Cached results so clicking a list row brings them back without re-running.
    QHash<QString, core::TagResult> m_results;
    QHash<QString, QString> m_failures; // rel → reason

    void onRun();
    void onCancel();
    void onScanned(int total);
    void onImageTagged(QString relPath, core::TagResult result);
    void onImageFailed(QString relPath, QString reason);
    void onProgress(int done, int total);
    void onFinished(bool cancelled);

    void onResultRowChanged(QListWidgetItem* current, QListWidgetItem* prev);
    void showFocusedResult();

    void setRunning(bool on);
    void persistSettings();
    // Toggles between the centered placeholder (with `message`) and the
    // results list. Empty `message` → results list visible.
    void showEmptyState(const QString& message);
};

} // namespace gui
