#pragma once
#include <core/settings.h>
#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;

namespace tc {

class AppData;

// Edits the live Settings in place and says so. Nothing here writes the file;
// the shell owns settings.json and saves on settingsChanged.
class SettingsPage : public QWidget {
    Q_OBJECT

public:
    SettingsPage(Settings& settings, AppData& data, QWidget* parent = nullptr);

    // Driven by the shell's ComfyUI connection state.
    void setComfyStatus(bool connected, const QString& error = {});

public slots:
    // Pages are built before the data dir is read, so every widget starts on
    // the struct defaults. This puts the loaded values in without emitting.
    void reload();

    void appendLogMessage(const QString& message);

signals:
    void settingsChanged();
    void reconnectRequested();
    void exportEntriesRequested();
    void importEntriesRequested();
    void clearUnusedInputsRequested();
    void purgeTagDefinitionsRequested();
    void purgeUnknownFacetsRequested();

private:
    void onComfyToggled(bool enabled);
    void rebuildFacetFormats();
    QWidget* makeFacetFormatRow(int index, bool isAddRow);

    Settings* m_settings = nullptr;
    AppData* m_data = nullptr;

    // ---- Appearance
    QDoubleSpinBox* m_tileGradStart = nullptr;
    QSpinBox* m_tileGradAlpha = nullptr;
    QPushButton* m_tileTitleColor = nullptr;
    QSlider* m_sfxVolume = nullptr;
    QLabel* m_sfxVolumeValue = nullptr;

    // ---- ComfyUI
    QCheckBox* m_enableComfy = nullptr;
    QLineEdit* m_serverAddress = nullptr;
    QLineEdit* m_apiKey = nullptr;
    QLineEdit* m_outputFolder = nullptr;
    QLineEdit* m_tempFolder = nullptr;
    QLineEdit* m_loraBaseDir = nullptr;
    QLineEdit* m_loraTestDir = nullptr;
    QDoubleSpinBox* m_defaultLoraModelStrength = nullptr;
    QDoubleSpinBox* m_defaultLoraClipStrength = nullptr;
    QLineEdit* m_inputFolder = nullptr;
    QWidget* m_comfyDetails = nullptr; // hidden while the feature is off
    QLabel* m_statusDot = nullptr;
    QLabel* m_statusText = nullptr;

    // ---- Facets
    QLineEdit* m_quickCharFacet = nullptr;
    QLineEdit* m_quickCopyFacet = nullptr;
    QLineEdit* m_quickTriggerFacet = nullptr;
    QLineEdit* m_quickStyleFacet = nullptr;
    QWidget* m_formatsContainer = nullptr;

    // ---- Composer
    QCheckBox* m_forceOverwriteRules = nullptr;

    QPlainTextEdit* m_log = nullptr;
};

} // namespace tc
