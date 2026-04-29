#pragma once
#include <utils/appsettings.h>
#include <QWidget>
#include <QCheckBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QPlainTextEdit>

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

private:
    void onComfyToggled(bool enabled);

    utils::AppSettings* m_settings;

    // Appearance section
    QCheckBox*      m_enableDanmaku;

    // ComfyUI section
    QCheckBox*      m_enableComfyUi;
    QLineEdit*      m_serverAddress;
    QLineEdit*      m_apiKey;
    QLineEdit*      m_outputFolder;
    QLineEdit*      m_tempFolder;
    QLineEdit*      m_loraBaseDir;
    QWidget*        m_comfyDetails; // shown/hidden by toggle
    QLabel*         m_statusDot;
    QLabel*         m_statusText;

    // Log section
    QPlainTextEdit* m_log;
};

} // namespace gui
