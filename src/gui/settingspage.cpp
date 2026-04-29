#include <gui/settingspage.h>
#include <utils/logger.h>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QStyle>
#include <QFrame>
#include <QFileDialog>

namespace gui {

SettingsPage::SettingsPage(utils::AppSettings* settings, QWidget* parent)
    : QWidget(parent)
    , m_settings(settings)
{
    setObjectName("SettingsPage");
    setAttribute(Qt::WA_StyledBackground, true);

    // ── Scrollable body ───────────────────────────────────────────────────────
    auto* body = new QWidget;
    body->setObjectName("SettingsBody");

    auto* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(32, 24, 32, 24);
    bodyLayout->setSpacing(0);

    // ── Section: Appearance ───────────────────────────────────────────────────
    auto* appearanceHeader = new QLabel("APPEARANCE");
    appearanceHeader->setObjectName("SettingsSectionHeader");
    bodyLayout->addWidget(appearanceHeader);
    bodyLayout->addSpacing(12);

    auto* appearanceGroup = new QWidget;
    appearanceGroup->setObjectName("SettingsGroup");
    appearanceGroup->setAttribute(Qt::WA_StyledBackground, true);
    auto* appearanceLayout = new QVBoxLayout(appearanceGroup);
    appearanceLayout->setContentsMargins(16, 14, 16, 14);
    appearanceLayout->setSpacing(8);

    m_enableDanmaku = new QCheckBox("Background animation (danmaku)");
    m_enableDanmaku->setObjectName("SettingsCheckBox");
    m_enableDanmaku->setChecked(settings->danmakuEnabled);
    appearanceLayout->addWidget(m_enableDanmaku);

    auto* danmakuHint = new QLabel("キタ━━━(゜∀゜)━━━!!!!!");
    danmakuHint->setObjectName("SettingsHintLabel");
    appearanceLayout->addWidget(danmakuHint);

    bodyLayout->addWidget(appearanceGroup);
    bodyLayout->addSpacing(24);

    // ── Section: Backends ─────────────────────────────────────────────────────
    auto* backendsHeader = new QLabel("BACKENDS");
    backendsHeader->setObjectName("SettingsSectionHeader");
    bodyLayout->addWidget(backendsHeader);
    bodyLayout->addSpacing(12);

    // ── ComfyUI group ─────────────────────────────────────────────────────────
    auto* comfyGroup = new QWidget;
    comfyGroup->setObjectName("SettingsGroup");
    comfyGroup->setAttribute(Qt::WA_StyledBackground, true);
    auto* comfyLayout = new QVBoxLayout(comfyGroup);
    comfyLayout->setContentsMargins(16, 14, 16, 14);
    comfyLayout->setSpacing(12);

    // Toggle row
    auto* toggleRow = new QHBoxLayout;
    toggleRow->setSpacing(10);

    m_enableComfyUi = new QCheckBox("ComfyUI");
    m_enableComfyUi->setObjectName("SettingsCheckBox");
    m_enableComfyUi->setChecked(settings->comfyUiEnabled);
    toggleRow->addWidget(m_enableComfyUi);
    toggleRow->addStretch();

    m_statusDot  = new QLabel("●");
    m_statusText = new QLabel("disabled");
    m_statusDot ->setObjectName("SettingsStatusDot");
    m_statusText->setObjectName("SettingsStatusText");
    toggleRow->addWidget(m_statusDot);
    toggleRow->addWidget(m_statusText);

    comfyLayout->addLayout(toggleRow);

    // ── Detail fields (shown when enabled) ────────────────────────────────────
    m_comfyDetails = new QWidget;
    auto* detailLayout = new QGridLayout(m_comfyDetails);
    detailLayout->setContentsMargins(0, 4, 0, 0);
    detailLayout->setHorizontalSpacing(12);
    detailLayout->setVerticalSpacing(10);
    detailLayout->setColumnStretch(1, 1);

    auto makeLabel = [](const QString& text) {
        auto* l = new QLabel(text);
        l->setObjectName("SettingsFieldLabel");
        return l;
    };

    m_serverAddress = new QLineEdit;
    m_serverAddress->setObjectName("SettingsInput");
    m_serverAddress->setPlaceholderText("127.0.0.1:8188");
    m_serverAddress->setText(settings->comfyUiServerAddress);

    m_apiKey = new QLineEdit;
    m_apiKey->setObjectName("SettingsInput");
    m_apiKey->setPlaceholderText("API key (leave blank for local)");
    m_apiKey->setText(settings->comfyUiApiKey);
    m_apiKey->setEchoMode(QLineEdit::Password);

    m_outputFolder = new QLineEdit;
    m_outputFolder->setObjectName("SettingsInput");
    m_outputFolder->setPlaceholderText("e.g. C:/ComfyUI/output/{yyyy-MM-dd}");
    m_outputFolder->setText(settings->comfyUiOutputFolder);

    auto* browseBtn = new QPushButton("Browse");
    browseBtn->setObjectName("SettingsBrowseBtn");
    browseBtn->setCursor(Qt::PointingHandCursor);
    browseBtn->setFixedWidth(70);

    auto* outputRow = new QHBoxLayout;
    outputRow->setSpacing(6);
    outputRow->addWidget(m_outputFolder, 1);
    outputRow->addWidget(browseBtn);

    auto* outputHint = new QLabel("Use {yyyy-MM-dd} for date-based subfolders");
    outputHint->setObjectName("SettingsHintLabel");

    m_tempFolder = new QLineEdit;
    m_tempFolder->setObjectName("SettingsInput");
    m_tempFolder->setPlaceholderText("e.g. C:/ComfyUI/temp");
    m_tempFolder->setText(settings->comfyUiTempFolder);

    auto* tempBrowseBtn = new QPushButton("Browse");
    tempBrowseBtn->setObjectName("SettingsBrowseBtn");
    tempBrowseBtn->setCursor(Qt::PointingHandCursor);
    tempBrowseBtn->setFixedWidth(70);

    auto* tempRow = new QHBoxLayout;
    tempRow->setSpacing(6);
    tempRow->addWidget(m_tempFolder, 1);
    tempRow->addWidget(tempBrowseBtn);

    m_loraBaseDir = new QLineEdit;
    m_loraBaseDir->setObjectName("SettingsInput");
    m_loraBaseDir->setPlaceholderText("e.g. C:/ComfyUI/models/loras");
    m_loraBaseDir->setText(settings->loraBaseDir);

    auto* loraBrowseBtn = new QPushButton("Browse");
    loraBrowseBtn->setObjectName("SettingsBrowseBtn");
    loraBrowseBtn->setCursor(Qt::PointingHandCursor);
    loraBrowseBtn->setFixedWidth(70);

    auto* loraRow = new QHBoxLayout;
    loraRow->setSpacing(6);
    loraRow->addWidget(m_loraBaseDir, 1);
    loraRow->addWidget(loraBrowseBtn);

    auto* connectBtn = new QPushButton("Connect");
    connectBtn->setObjectName("SettingsConnectBtn");
    connectBtn->setCursor(Qt::PointingHandCursor);

    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setObjectName("SettingsSeparator");

    detailLayout->addWidget(sep,                          0, 0, 1, 2);
    detailLayout->addWidget(makeLabel("Server address"),   1, 0);
    detailLayout->addWidget(m_serverAddress,               1, 1);
    detailLayout->addWidget(makeLabel("API key"),          2, 0);
    detailLayout->addWidget(m_apiKey,                     2, 1);
    detailLayout->addWidget(makeLabel("Output folder"),    3, 0);
    detailLayout->addLayout(outputRow,                     3, 1);
    detailLayout->addWidget(outputHint,                    4, 1);
    detailLayout->addWidget(makeLabel("Temp folder"),      5, 0);
    detailLayout->addLayout(tempRow,                       5, 1);
    detailLayout->addWidget(makeLabel("LoRA folder"),      6, 0);
    detailLayout->addLayout(loraRow,                       6, 1);
    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);
    btnRow->addWidget(connectBtn);
    btnRow->addStretch();
    detailLayout->addLayout(btnRow,                        7, 1);

    comfyLayout->addWidget(m_comfyDetails);
    bodyLayout->addWidget(comfyGroup);
    bodyLayout->addSpacing(24);

    // ── Section: Log ──────────────────────────────────────────────────────────
    auto* logHeader = new QLabel("LOG");
    logHeader->setObjectName("SettingsSectionHeader");
    bodyLayout->addWidget(logHeader);
    bodyLayout->addSpacing(12);

    m_log = new QPlainTextEdit;
    m_log->setObjectName("LogView");
    m_log->setReadOnly(true);
    m_log->setUndoRedoEnabled(false);
    m_log->setMaximumBlockCount(5000);
    m_log->setMinimumHeight(200);
    for (const QString& msg : utils::Logger::instance().history())
        m_log->appendPlainText(msg);
    bodyLayout->addWidget(m_log);

    bodyLayout->addStretch();

    // ── Scroll area ───────────────────────────────────────────────────────────
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("SettingsScroll");
    scroll->setWidget(body);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(scroll);

    // ── Connections ───────────────────────────────────────────────────────────
    connect(m_enableDanmaku, &QCheckBox::toggled, this, [this](bool on) {
        m_settings->danmakuEnabled = on;
        emit settingsChanged();
    });

    connect(m_enableComfyUi, &QCheckBox::toggled, this, &SettingsPage::onComfyToggled);

    connect(m_serverAddress, &QLineEdit::editingFinished, this, [this]() {
        m_settings->comfyUiServerAddress = m_serverAddress->text().trimmed();
        if (m_settings->comfyUiServerAddress.isEmpty())
            m_settings->comfyUiServerAddress = "127.0.0.1:8188";
        emit settingsChanged();
    });

    connect(m_apiKey, &QLineEdit::editingFinished, this, [this]() {
        m_settings->comfyUiApiKey = m_apiKey->text().trimmed();
        emit settingsChanged();
    });

    connect(m_outputFolder, &QLineEdit::editingFinished, this, [this]() {
        m_settings->comfyUiOutputFolder = m_outputFolder->text().trimmed();
        emit settingsChanged();
    });

    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        // Strip date-pattern tokens so we can navigate to a real path
        const QString raw      = m_outputFolder->text().trimmed();
        const QString startDir = raw.section(QLatin1Char('{'), 0, 0).trimmed();
        const QString dir = QFileDialog::getExistingDirectory(
            this, "Select ComfyUI Output Folder", startDir);
        if (dir.isEmpty()) return;
        // Append the date-pattern suffix the user had typed, if any
        const int bracePos = raw.indexOf(QLatin1Char('{'));
        const QString newPath = bracePos >= 0
            ? dir + "/" + raw.mid(bracePos)
            : dir;
        m_outputFolder->setText(newPath);
        m_settings->comfyUiOutputFolder = newPath;
        emit settingsChanged();
    });

    connect(m_tempFolder, &QLineEdit::editingFinished, this, [this]() {
        m_settings->comfyUiTempFolder = m_tempFolder->text().trimmed();
        emit settingsChanged();
    });

    connect(tempBrowseBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, "Select ComfyUI Temp Folder", m_tempFolder->text().trimmed());
        if (dir.isEmpty()) return;
        m_tempFolder->setText(dir);
        m_settings->comfyUiTempFolder = dir;
        emit settingsChanged();
    });

    connect(m_loraBaseDir, &QLineEdit::editingFinished, this, [this]() {
        m_settings->loraBaseDir = m_loraBaseDir->text().trimmed();
        emit settingsChanged();
    });

    connect(loraBrowseBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, "Select LoRA Base Folder", m_loraBaseDir->text().trimmed());
        if (dir.isEmpty()) return;
        m_loraBaseDir->setText(dir);
        m_settings->loraBaseDir = dir;
        emit settingsChanged();
    });

    connect(connectBtn,  &QPushButton::clicked, this, &SettingsPage::reconnectRequested);

    connect(&utils::Logger::instance(), &utils::Logger::messageLogged,
            this, &SettingsPage::appendLogMessage);

    // Apply initial visibility
    onComfyToggled(settings->comfyUiEnabled);
}

void SettingsPage::appendLogMessage(const QString& message)
{
    m_log->appendPlainText(message);
    m_log->ensureCursorVisible();
}

void SettingsPage::onComfyToggled(bool enabled)
{
    m_settings->comfyUiEnabled = enabled;
    m_comfyDetails->setVisible(enabled);

    if (!enabled) {
        m_statusDot ->setProperty("status", "disabled");
        m_statusText->setText("disabled");
    } else {
        m_statusDot ->setProperty("status", "connecting");
        m_statusText->setText("connecting…");
    }
    // Force style re-evaluation after property change
    m_statusDot->style()->unpolish(m_statusDot);
    m_statusDot->style()->polish(m_statusDot);

    emit settingsChanged();
}

void SettingsPage::setComfyStatus(bool connected, const QString& error)
{
    if (!m_settings->comfyUiEnabled) return;

    if (!error.isEmpty()) {
        m_statusDot ->setProperty("status", "error");
        m_statusText->setText(error);
    } else if (connected) {
        m_statusDot ->setProperty("status", "connected");
        m_statusText->setText("connected");
    } else {
        m_statusDot ->setProperty("status", "disconnected");
        m_statusText->setText("disconnected");
    }
    m_statusDot->style()->unpolish(m_statusDot);
    m_statusDot->style()->polish(m_statusDot);
}

} // namespace gui
