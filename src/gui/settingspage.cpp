#include <gui/settingspage.h>
#include <gui/chromeddialog.h>
#include <utils/logger.h>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QStyle>
#include <QFrame>
#include <QFileDialog>
#include <QAbstractSpinBox>
#include <QColorDialog>
#include <QDialogButtonBox>

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

    // ── Tile gradient ─────────────────────────────────────────────────────
    auto* tileGradGrid = new QGridLayout;
    tileGradGrid->setContentsMargins(0, 6, 0, 0);
    tileGradGrid->setHorizontalSpacing(12);
    tileGradGrid->setVerticalSpacing(8);
    tileGradGrid->setColumnStretch(1, 1);

    auto makeGradLabel = [](const QString& text) {
        auto* l = new QLabel(text);
        l->setObjectName("SettingsFieldLabel");
        return l;
    };

    m_tileGradStart = new QDoubleSpinBox;
    m_tileGradStart->setObjectName("SettingsInput");
    m_tileGradStart->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_tileGradStart->setRange(0.0, 1.0);
    m_tileGradStart->setDecimals(2);
    m_tileGradStart->setSingleStep(0.05);
    m_tileGradStart->setValue(settings->tileGradientStart);
    m_tileGradStart->setFixedWidth(80);

    m_tileGradAlpha = new QSpinBox;
    m_tileGradAlpha->setObjectName("SettingsInput");
    m_tileGradAlpha->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_tileGradAlpha->setRange(0, 255);
    m_tileGradAlpha->setValue(settings->tileGradientAlpha);
    m_tileGradAlpha->setFixedWidth(80);

    auto wrapLeft = [](QWidget* w) {
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(w);
        row->addStretch();
        return row;
    };

    m_tileTitleColor = new QPushButton;
    m_tileTitleColor->setFixedSize(110, 32);
    m_tileTitleColor->setCursor(Qt::PointingHandCursor);

    // Pick a readable foreground (black on light backgrounds, white on dark)
    // so the hex string stays legible regardless of the chosen colour.
    auto applyTitleSwatch = [this]() {
        const QColor c(m_settings->tileTitleColor);
        const bool dark = c.isValid() &&
                          (0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue()) < 128;
        m_tileTitleColor->setStyleSheet(
            QString("background-color: %1; color: %2; "
                    "border: 1px solid #2a2a2a; border-radius: 3px; "
                    "font-family: monospace; font-size: 12px;")
                .arg(m_settings->tileTitleColor, dark ? "#ffffff" : "#000000"));
        m_tileTitleColor->setText(m_settings->tileTitleColor);
    };
    applyTitleSwatch();

    connect(m_tileTitleColor, &QPushButton::clicked, this,
        [this, applyTitleSwatch]() {
            // Embed Qt's built-in QColorDialog as a widget inside a
            // ChromedDialog so the picker carries the same custom titlebar
            // and resize behaviour as the rest of the app's modals.
            const QColor initial(m_settings->tileTitleColor);

            ChromedDialog wrapper(this);
            wrapper.setWindowTitle("Tile title colour");

            auto* picker = new QColorDialog(
                initial.isValid() ? initial : Qt::white, wrapper.contentArea());
            picker->setOptions(QColorDialog::DontUseNativeDialog
                             | QColorDialog::NoButtons);
            picker->setWindowFlags(Qt::Widget);  // embed as child, not top-level
            picker->setSizeGripEnabled(false);

            auto* btns = new QDialogButtonBox(
                QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                wrapper.contentArea());
            connect(btns, &QDialogButtonBox::accepted, &wrapper, &QDialog::accept);
            connect(btns, &QDialogButtonBox::rejected, &wrapper, &QDialog::reject);
            // Double-click on a swatch counts as confirmation, just like the
            // native dialog.
            connect(picker, &QColorDialog::colorSelected,
                    &wrapper, [&wrapper](const QColor&) { wrapper.accept(); });

            auto* layout = new QVBoxLayout(wrapper.contentArea());
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(8);
            layout->addWidget(picker, 1);
            layout->addWidget(btns);

            if (wrapper.exec() != QDialog::Accepted) return;
            const QColor chosen = picker->currentColor();
            if (!chosen.isValid()) return;
            m_settings->tileTitleColor = chosen.name();
            applyTitleSwatch();
            emit settingsChanged();
        });

    tileGradGrid->addWidget(makeGradLabel("Tile gradient start (0–1)"), 0, 0);
    tileGradGrid->addLayout(wrapLeft(m_tileGradStart),                  0, 1);
    tileGradGrid->addWidget(makeGradLabel("Tile gradient opacity (0–255)"), 1, 0);
    tileGradGrid->addLayout(wrapLeft(m_tileGradAlpha),                  1, 1);
    tileGradGrid->addWidget(makeGradLabel("Tile title colour"),         2, 0);
    tileGradGrid->addLayout(wrapLeft(m_tileTitleColor),                 2, 1);

    auto* tileGradHint = new QLabel(
        "Bottom fade and title text behind tile thumbnails. Restart to apply.");
    tileGradHint->setObjectName("SettingsHintLabel");
    tileGradHint->setWordWrap(true);

    appearanceLayout->addLayout(tileGradGrid);
    appearanceLayout->addWidget(tileGradHint);

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

    m_inputFolder = new QLineEdit;
    m_inputFolder->setObjectName("SettingsInput");
    m_inputFolder->setPlaceholderText("e.g. C:/ComfyUI/input  (optional - enables direct file copy)");
    m_inputFolder->setText(settings->comfyUiInputFolder);

    auto* inputBrowseBtn = new QPushButton("Browse");
    inputBrowseBtn->setObjectName("SettingsBrowseBtn");
    inputBrowseBtn->setCursor(Qt::PointingHandCursor);
    inputBrowseBtn->setFixedWidth(70);

    auto* inputRow = new QHBoxLayout;
    inputRow->setSpacing(6);
    inputRow->addWidget(m_inputFolder, 1);
    inputRow->addWidget(inputBrowseBtn);

    auto* inputHint = new QLabel(
        "Image-typed workflow vars upload to ComfyUI on each run. "
        "Set this to ComfyUI's input/ folder to skip HTTP and copy directly.");
    inputHint->setObjectName("SettingsHintLabel");
    inputHint->setWordWrap(true);

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
    detailLayout->addWidget(makeLabel("Input folder"),     7, 0);
    detailLayout->addLayout(inputRow,                      7, 1);
    detailLayout->addWidget(inputHint,                     8, 1);
    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);
    btnRow->addWidget(connectBtn);
    btnRow->addStretch();
    detailLayout->addLayout(btnRow,                        9, 1);

    comfyLayout->addWidget(m_comfyDetails);
    bodyLayout->addWidget(comfyGroup);
    bodyLayout->addSpacing(24);

    // ── Section: Facets ───────────────────────────────────────────────────────
    auto* facetsHeader = new QLabel("FACETS");
    facetsHeader->setObjectName("SettingsSectionHeader");
    bodyLayout->addWidget(facetsHeader);
    bodyLayout->addSpacing(12);

    auto* facetsGroup = new QWidget;
    facetsGroup->setObjectName("SettingsGroup");
    facetsGroup->setAttribute(Qt::WA_StyledBackground, true);
    auto* facetsLayout = new QGridLayout(facetsGroup);
    facetsLayout->setContentsMargins(16, 14, 16, 14);
    facetsLayout->setHorizontalSpacing(12);
    facetsLayout->setVerticalSpacing(10);
    facetsLayout->setColumnStretch(1, 1);

    m_quickCharFacet = new QLineEdit;
    m_quickCharFacet->setObjectName("SettingsInput");
    m_quickCharFacet->setPlaceholderText("rcharacter");
    m_quickCharFacet->setText(settings->quickCharacterFacet);

    m_quickCopyFacet = new QLineEdit;
    m_quickCopyFacet->setObjectName("SettingsInput");
    m_quickCopyFacet->setPlaceholderText("rcopyright");
    m_quickCopyFacet->setText(settings->quickCopyrightFacet);

    m_quickTriggerFacet = new QLineEdit;
    m_quickTriggerFacet->setObjectName("SettingsInput");
    m_quickTriggerFacet->setPlaceholderText("rtrigger_word");
    m_quickTriggerFacet->setText(settings->quickTriggerWordFacet);

    m_quickStyleFacet = new QLineEdit;
    m_quickStyleFacet->setObjectName("SettingsInput");
    m_quickStyleFacet->setPlaceholderText("rstyle");
    m_quickStyleFacet->setText(settings->quickStyleFacet);

    auto* facetsHint = new QLabel(
        "Names of facets used by the composer's right-click \"Quick add\" actions.");
    facetsHint->setObjectName("SettingsHintLabel");
    facetsHint->setWordWrap(true);

    facetsLayout->addWidget(makeLabel("Quick character facet"),    0, 0);
    facetsLayout->addWidget(m_quickCharFacet,                       0, 1);
    facetsLayout->addWidget(makeLabel("Quick copyright facet"),    1, 0);
    facetsLayout->addWidget(m_quickCopyFacet,                       1, 1);
    facetsLayout->addWidget(makeLabel("Quick trigger word facet"), 2, 0);
    facetsLayout->addWidget(m_quickTriggerFacet,                    2, 1);
    facetsLayout->addWidget(makeLabel("Quick style facet"),        3, 0);
    facetsLayout->addWidget(m_quickStyleFacet,                      3, 1);
    facetsLayout->addWidget(facetsHint,                             4, 1);

    bodyLayout->addWidget(facetsGroup);
    bodyLayout->addSpacing(24);

    // ── Section: Data (import / export) ───────────────────────────────────────
    auto* dataHeader = new QLabel("DATA");
    dataHeader->setObjectName("SettingsSectionHeader");
    bodyLayout->addWidget(dataHeader);
    bodyLayout->addSpacing(12);

    auto* dataGroup = new QWidget;
    dataGroup->setObjectName("SettingsGroup");
    dataGroup->setAttribute(Qt::WA_StyledBackground, true);
    auto* dataLayout = new QVBoxLayout(dataGroup);
    dataLayout->setContentsMargins(16, 14, 16, 14);
    dataLayout->setSpacing(10);

    auto* dataHint = new QLabel(
        "Export selected entries (with referenced tag definitions) to a folder, "
        "or import a previously-exported folder. Imports merge into your data; "
        "duplicate entries are skipped, and tag-definition collisions are handled "
        "per the option selected in the import dialog.");
    dataHint->setObjectName("SettingsHintLabel");
    dataHint->setWordWrap(true);
    dataLayout->addWidget(dataHint);

    auto* dataBtnRow = new QHBoxLayout;
    auto* exportBtn  = new QPushButton("Export entries…");
    exportBtn->setObjectName("SettingsLaunchBtn");
    exportBtn->setCursor(Qt::PointingHandCursor);
    auto* importBtn  = new QPushButton("Import entries…");
    importBtn->setObjectName("SettingsLaunchBtn");
    importBtn->setCursor(Qt::PointingHandCursor);
    dataBtnRow->addWidget(exportBtn);
    dataBtnRow->addWidget(importBtn);
    dataBtnRow->addStretch();
    dataLayout->addLayout(dataBtnRow);

    connect(exportBtn, &QPushButton::clicked, this, &SettingsPage::exportEntriesRequested);
    connect(importBtn, &QPushButton::clicked, this, &SettingsPage::importEntriesRequested);

    bodyLayout->addWidget(dataGroup);
    bodyLayout->addSpacing(24);

    // ── Section: Workflow input images ────────────────────────────────────────
    auto* inputsHeader = new QLabel("INPUT IMAGES");
    inputsHeader->setObjectName("SettingsSectionHeader");
    bodyLayout->addWidget(inputsHeader);
    bodyLayout->addSpacing(12);

    auto* inputsGroup = new QWidget;
    inputsGroup->setObjectName("SettingsGroup");
    inputsGroup->setAttribute(Qt::WA_StyledBackground, true);
    auto* inputsLayout = new QVBoxLayout(inputsGroup);
    inputsLayout->setContentsMargins(16, 14, 16, 14);
    inputsLayout->setSpacing(10);

    auto* inputsHint = new QLabel(
        "Workflow image inputs, painted masks, and rendered edit variants are "
        "cached on disk. This removes any cache entry that no current workflow "
        "variable references - useful after deleting workflows or replacing "
        "image inputs.");
    inputsHint->setObjectName("SettingsHintLabel");
    inputsHint->setWordWrap(true);
    inputsLayout->addWidget(inputsHint);

    auto* inputsBtnRow = new QHBoxLayout;
    auto* clearInputsBtn = new QPushButton("Clear unused inputs");
    clearInputsBtn->setObjectName("SettingsLaunchBtn");
    clearInputsBtn->setCursor(Qt::PointingHandCursor);
    inputsBtnRow->addWidget(clearInputsBtn);
    inputsBtnRow->addStretch();
    inputsLayout->addLayout(inputsBtnRow);

    connect(clearInputsBtn, &QPushButton::clicked,
            this, &SettingsPage::clearUnusedInputsRequested);

    bodyLayout->addWidget(inputsGroup);
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
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(scroll);

    // ── Connections ───────────────────────────────────────────────────────────
    connect(m_enableDanmaku, &QCheckBox::toggled, this, [this](bool on) {
        m_settings->danmakuEnabled = on;
        emit settingsChanged();
    });

    // Tile-gradient values are read once at startup by EntryView, so these
    // just persist to the settings file - they take effect on next launch.
    connect(m_tileGradStart, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
        m_settings->tileGradientStart = v;
        emit settingsChanged();
    });
    connect(m_tileGradAlpha, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v) {
        m_settings->tileGradientAlpha = v;
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

    connect(m_inputFolder, &QLineEdit::editingFinished, this, [this]() {
        m_settings->comfyUiInputFolder = m_inputFolder->text().trimmed();
        emit settingsChanged();
    });

    connect(inputBrowseBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, "Select ComfyUI Input Folder", m_inputFolder->text().trimmed());
        if (dir.isEmpty()) return;
        m_inputFolder->setText(dir);
        m_settings->comfyUiInputFolder = dir;
        emit settingsChanged();
    });

    // Empty value disables the corresponding menu item - placeholder text
    // shows a conventional name as a suggestion, not as a fallback.
    connect(m_quickCharFacet, &QLineEdit::editingFinished, this, [this]() {
        m_settings->quickCharacterFacet = m_quickCharFacet->text().trimmed();
        emit settingsChanged();
    });

    connect(m_quickCopyFacet, &QLineEdit::editingFinished, this, [this]() {
        m_settings->quickCopyrightFacet = m_quickCopyFacet->text().trimmed();
        emit settingsChanged();
    });

    connect(m_quickTriggerFacet, &QLineEdit::editingFinished, this, [this]() {
        m_settings->quickTriggerWordFacet = m_quickTriggerFacet->text().trimmed();
        emit settingsChanged();
    });

    connect(m_quickStyleFacet, &QLineEdit::editingFinished, this, [this]() {
        m_settings->quickStyleFacet = m_quickStyleFacet->text().trimmed();
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
