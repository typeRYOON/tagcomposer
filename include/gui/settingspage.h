#pragma once
#include <utils/appsettings.h>
#include <QWidget>
#include <QCheckBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>

namespace gui {

class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(utils::AppSettings* settings, QWidget* parent = nullptr);

    // Call from AppMainWindow to update the WS status indicator
    void setComfyStatus(bool connected, const QString& error = {});

public slots:
    void appendLogMessage(const QString& message);

signals:
    void settingsChanged();
    void reconnectRequested();
    void exportEntriesRequested();
    void importEntriesRequested();
    void clearUnusedInputsRequested();
    void purgeTagDefinitionsRequested();

private:
    void onComfyToggled(bool enabled);

    utils::AppSettings* m_settings;

    // Appearance section
    QCheckBox* m_enableDanmaku;
    QDoubleSpinBox* m_tileGradStart;
    QSpinBox* m_tileGradAlpha;
    QPushButton* m_tileTitleColor;

    // ComfyUI section
    QCheckBox* m_enableComfyUi;
    QLineEdit* m_serverAddress;
    QLineEdit* m_apiKey;
    QLineEdit* m_outputFolder;
    QLineEdit* m_tempFolder;
    QLineEdit* m_loraBaseDir;
    QLineEdit* m_loraTestDir;
    QDoubleSpinBox* m_defaultLoraModelStr;
    QDoubleSpinBox* m_defaultLoraClipStr;
    QLineEdit* m_inputFolder;
    QWidget* m_comfyDetails; // shown/hidden by toggle
    QLabel* m_statusDot;
    QLabel* m_statusText;

    // Facets section
    QLineEdit* m_quickCharFacet;
    QLineEdit* m_quickCopyFacet;
    QLineEdit* m_quickTriggerFacet;
    QLineEdit* m_quickStyleFacet;

    // Log section
    QPlainTextEdit* m_log;
};

} // namespace gui
