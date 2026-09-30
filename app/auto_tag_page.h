#pragma once
#include <tagger/tagger_model.h>
#include <QHash>
#include <QString>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QProgressBar;
class QPushButton;
class QSlider;
class QSpinBox;

namespace tc {

class BatchTagger;
struct Settings;
class TaggerLibrary;

// Runs a tagger model over a folder and writes a .txt beside each image.
// Inference lives on a worker thread inside BatchTagger; this page drives it
// and shows what came back, including the tags that just missed the
// threshold, so the slider can be judged without another run.
class AutoTagPage : public QWidget {
    Q_OBJECT

public:
    AutoTagPage(TaggerLibrary& library, Settings& settings, QWidget* parent = nullptr);
    ~AutoTagPage() override;

    // Re-reads the model folders.
    void refreshModels();

    // The handoff from the collector, which has already picked the folder.
    void setInputFolder(const QString& folder);

signals:
    void editFolderRequested(const QString& folder);
    void sendToBatchEditRequested(const QString& folder);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void run();
    void cancel();

    void onScanned(int total);
    void onImageTagged(const QString& relativePath, const TaggerResult& result);
    void onImageFailed(const QString& relativePath, const QString& reason);
    void onFinished(bool cancelled);

    void showFocused();
    void setRunning(bool running);
    void persistSettings();

    // An empty message shows the results list instead.
    void showEmptyState(const QString& message);

    TaggerLibrary* m_library = nullptr;
    Settings* m_settings = nullptr;
    BatchTagger* m_runner = nullptr;

    QComboBox* m_models = nullptr;
    QLineEdit* m_inputEdit = nullptr;
    QPushButton* m_inputBrowse = nullptr;
    QLineEdit* m_outputEdit = nullptr;
    QPushButton* m_outputBrowse = nullptr;
    QSlider* m_threshold = nullptr;
    QLabel* m_thresholdValue = nullptr;
    QSpinBox* m_cooldown = nullptr;
    QCheckBox* m_recursive = nullptr;
    QCheckBox* m_moveImages = nullptr;
    QPushButton* m_runBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QPushButton* m_sendToEditor = nullptr;
    QPushButton* m_sendToBatch = nullptr;

    QLabel* m_status = nullptr;
    QProgressBar* m_progress = nullptr;
    QListWidget* m_results = nullptr;
    QWidget* m_emptyState = nullptr;
    QLabel* m_emptyLabel = nullptr;

    QLabel* m_focusImage = nullptr;
    QLabel* m_focusRating = nullptr;
    QListWidget* m_focusTags = nullptr;
    QString m_focused;

    // Kept so clicking back through the list costs nothing.
    QHash<QString, TaggerResult> m_tagged;
    QHash<QString, QString> m_failed; // relative path to reason
};

} // namespace tc
