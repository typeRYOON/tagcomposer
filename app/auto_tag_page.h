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

// Runs a tagger model over a folder, writing a .txt beside each image. Shows
// each result, including near misses, so the threshold can be judged.
class AutoTagPage : public QWidget {
    Q_OBJECT

public:
    AutoTagPage(TaggerLibrary& library, Settings& settings, QWidget* parent = nullptr);
    ~AutoTagPage() override;

    void refreshModels();

    // Handoff from the collector.
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

    // An empty message shows the results list.
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

    // Cached for clicking back through the list.
    QHash<QString, TaggerResult> m_tagged;
    QHash<QString, QString> m_failed; // relative path -> reason
};

} // namespace tc
