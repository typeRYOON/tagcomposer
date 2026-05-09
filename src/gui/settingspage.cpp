#include <gui/settingspage.h>
#include <gui/chromeddialog.h>
#include <gui/widgets/appscrollbar.h>
#include <core/soundplayer.h>
#include <utils/appconfig.h>
#include <utils/logger.h>
#include <utils/qutils.h>
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
#include <QPixmap>

namespace gui {

SettingsPage::SettingsPage(utils::AppSettings* settings, QWidget* parent)
    : QWidget(parent), m_settings(settings)
{
    setObjectName("SettingsPage");
    setAttribute(Qt::WA_StyledBackground, true);

    // ---- Scrollable body
    auto* body = new QWidget;
    body->setObjectName("SettingsBody");

    auto* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(32, 24, 32, 12);
    bodyLayout->setSpacing(0);

    // ---- Section: Appearance
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

    // ---- Tile gradient
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

    auto applyTitleSwatch = [this]() {
        const QColor c(m_settings->tileTitleColor);
        const bool dark =
            c.isValid() && (0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue()) < 128;
        m_tileTitleColor->setStyleSheet(
            QString("background-color: %1; color: %2; "
                    "border: 1px solid #2a2a2a; border-radius: 3px; "
                    "font-family: monospace; font-size: 12px;")
                .arg(m_settings->tileTitleColor, dark ? "#ffffff" : "#000000"));
        m_tileTitleColor->setText(m_settings->tileTitleColor);
    };
    applyTitleSwatch();

    connect(m_tileTitleColor, &QPushButton::clicked, this, [this, applyTitleSwatch]() {
        const QColor initial(m_settings->tileTitleColor);

        ChromedDialog wrapper(this);
        wrapper.setWindowTitle("Tile title color");

        auto* picker =
            new QColorDialog(initial.isValid() ? initial : Qt::white, wrapper.contentArea());
        picker->setOptions(QColorDialog::DontUseNativeDialog | QColorDialog::NoButtons);
        picker->setWindowFlags(Qt::Widget); // embed as child, not top-level
        picker->setSizeGripEnabled(false);

        auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                          wrapper.contentArea());
        connect(btns, &QDialogButtonBox::accepted, &wrapper, &QDialog::accept);
        connect(btns, &QDialogButtonBox::rejected, &wrapper, &QDialog::reject);
        connect(picker, &QColorDialog::colorSelected, &wrapper,
                [&wrapper](const QColor&) { wrapper.accept(); });
        connect(picker, &QDialog::rejected, &wrapper, &QDialog::reject);

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
    tileGradGrid->addLayout(wrapLeft(m_tileGradStart), 0, 1);
    tileGradGrid->addWidget(makeGradLabel("Tile gradient opacity (0–255)"), 1, 0);
    tileGradGrid->addLayout(wrapLeft(m_tileGradAlpha), 1, 1);
    tileGradGrid->addWidget(makeGradLabel("Tile title color"), 2, 0);
    tileGradGrid->addLayout(wrapLeft(m_tileTitleColor), 2, 1);

    auto* tileGradHint =
        new QLabel("Bottom fade and title text behind tile thumbnails. Restart to apply.");
    tileGradHint->setObjectName("SettingsHintLabel");
    tileGradHint->setWordWrap(true);

    appearanceLayout->addLayout(tileGradGrid);
    appearanceLayout->addWidget(tileGradHint);

    // ---- Sound effect volume
    m_sfxVolume = new QSlider(Qt::Horizontal);
    m_sfxVolume->setObjectName("DatasetPmiSlider");
    m_sfxVolume->setRange(0, 100);
    m_sfxVolume->setValue(int(qBound(0.0f, settings->sfxVolume, 1.0f) * 100.0f));

    m_sfxVolumeValue = new QLabel;
    m_sfxVolumeValue->setObjectName("SettingsFieldLabel");
    m_sfxVolumeValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_sfxVolumeValue->setMinimumWidth(36);
    m_sfxVolumeValue->setText(QString::number(m_sfxVolume->value()) + "%");

    auto* sfxRow = new QHBoxLayout;
    sfxRow->setContentsMargins(0, 8, 0, 0);
    sfxRow->setSpacing(12);
    sfxRow->addWidget(makeGradLabel("Sound effect volume"));
    sfxRow->addWidget(m_sfxVolume, 1);
    sfxRow->addWidget(m_sfxVolumeValue);

    appearanceLayout->addLayout(sfxRow);

    bodyLayout->addWidget(appearanceGroup);
    bodyLayout->addSpacing(24);

    // ---- Section: Backends
    auto* backendsHeader = new QLabel("BACKENDS");
    backendsHeader->setObjectName("SettingsSectionHeader");
    bodyLayout->addWidget(backendsHeader);
    bodyLayout->addSpacing(12);

    // ---- ComfyUI group
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

    m_statusDot = new QLabel("●");
    m_statusText = new QLabel("disabled");
    m_statusDot->setObjectName("SettingsStatusDot");
    m_statusText->setObjectName("SettingsStatusText");
    toggleRow->addWidget(m_statusDot);
    toggleRow->addWidget(m_statusText);

    comfyLayout->addLayout(toggleRow);

    // ---- Detail fields (shown when enabled)
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

    m_loraTestDir = new QLineEdit;
    m_loraTestDir->setObjectName("SettingsInput");
    m_loraTestDir->setPlaceholderText(
        "Optional - secondary lora folder for testing (e.g. C:/Users/.../Downloads)");
    m_loraTestDir->setText(settings->loraTestDir);

    auto* loraTestBrowseBtn = new QPushButton("Browse");
    loraTestBrowseBtn->setObjectName("SettingsBrowseBtn");
    loraTestBrowseBtn->setCursor(Qt::PointingHandCursor);
    loraTestBrowseBtn->setFixedWidth(70);

    auto* loraTestRow = new QHBoxLayout;
    loraTestRow->setSpacing(6);
    loraTestRow->addWidget(m_loraTestDir, 1);
    loraTestRow->addWidget(loraTestBrowseBtn);

    auto* loraTestHint =
        new QLabel("Files dropped from this folder are recognised as already-placed and skip "
                   "the import dialog. Match this to ComfyUI's extra_model_paths.yaml entry.");
    loraTestHint->setObjectName("SettingsHintLabel");
    loraTestHint->setWordWrap(true);

    auto makeLoraSpin = [](double min, double max, double step, double val) {
        auto* s = new QDoubleSpinBox;
        s->setObjectName("LoraSpinBox");
        s->setButtonSymbols(QAbstractSpinBox::NoButtons);
        s->setRange(min, max);
        s->setSingleStep(step);
        s->setDecimals(2);
        s->setValue(val);
        s->setFixedWidth(48);
        return s;
    };
    auto makeLoraSpinLabel = [](const QString& t) {
        auto* l = new QLabel(t);
        l->setObjectName("LoraSpinLabel");
        return l;
    };

    m_defaultLoraModelStr = makeLoraSpin(0.0, 2.0, 0.05, settings->defaultLoraModelStr);
    m_defaultLoraClipStr = makeLoraSpin(0.0, 4.0, 0.10, settings->defaultLoraClipStr);

    auto* loraDefaultsRow = new QHBoxLayout;
    loraDefaultsRow->setContentsMargins(0, 0, 0, 0);
    loraDefaultsRow->setSpacing(6);
    loraDefaultsRow->addWidget(makeLoraSpinLabel("Model"));
    loraDefaultsRow->addWidget(m_defaultLoraModelStr);
    loraDefaultsRow->addSpacing(8);
    loraDefaultsRow->addWidget(makeLoraSpinLabel("Clip"));
    loraDefaultsRow->addWidget(m_defaultLoraClipStr);
    loraDefaultsRow->addStretch();

    auto* loraDefaultsHint =
        new QLabel("Applied when adding a new LoRA. Existing entries keep their values.");
    loraDefaultsHint->setObjectName("SettingsHintLabel");
    loraDefaultsHint->setWordWrap(true);

    m_inputFolder = new QLineEdit;
    m_inputFolder->setObjectName("SettingsInput");
    m_inputFolder->setPlaceholderText(
        "e.g. C:/ComfyUI/input  (optional - enables direct file copy)");
    m_inputFolder->setText(settings->comfyUiInputFolder);

    auto* inputBrowseBtn = new QPushButton("Browse");
    inputBrowseBtn->setObjectName("SettingsBrowseBtn");
    inputBrowseBtn->setCursor(Qt::PointingHandCursor);
    inputBrowseBtn->setFixedWidth(70);

    auto* inputRow = new QHBoxLayout;
    inputRow->setSpacing(6);
    inputRow->addWidget(m_inputFolder, 1);
    inputRow->addWidget(inputBrowseBtn);

    auto* inputHint =
        new QLabel("Image-typed workflow vars upload to ComfyUI on each run. "
                   "Set this to ComfyUI's input/ folder to skip HTTP and copy directly.");
    inputHint->setObjectName("SettingsHintLabel");
    inputHint->setWordWrap(true);

    auto* connectBtn = new QPushButton("Connect");
    connectBtn->setObjectName("SettingsConnectBtn");
    connectBtn->setCursor(Qt::PointingHandCursor);

    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setObjectName("SettingsSeparator");

    detailLayout->addWidget(sep, 0, 0, 1, 2);
    detailLayout->addWidget(makeLabel("Server address"), 1, 0);
    detailLayout->addWidget(m_serverAddress, 1, 1);
    detailLayout->addWidget(makeLabel("API key"), 2, 0);
    detailLayout->addWidget(m_apiKey, 2, 1);
    detailLayout->addWidget(makeLabel("Output folder"), 3, 0);
    detailLayout->addLayout(outputRow, 3, 1);
    detailLayout->addWidget(outputHint, 4, 1);
    detailLayout->addWidget(makeLabel("Temp folder"), 5, 0);
    detailLayout->addLayout(tempRow, 5, 1);
    detailLayout->addWidget(makeLabel("LoRA folder"), 6, 0);
    detailLayout->addLayout(loraRow, 6, 1);
    detailLayout->addWidget(makeLabel("LoRA test folder"), 7, 0);
    detailLayout->addLayout(loraTestRow, 7, 1);
    detailLayout->addWidget(loraTestHint, 8, 1);
    detailLayout->addWidget(makeLabel("LoRA defaults"), 9, 0);
    detailLayout->addLayout(loraDefaultsRow, 9, 1);
    detailLayout->addWidget(loraDefaultsHint, 10, 1);
    detailLayout->addWidget(makeLabel("Input folder"), 11, 0);
    detailLayout->addLayout(inputRow, 11, 1);
    detailLayout->addWidget(inputHint, 12, 1);
    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);
    btnRow->addWidget(connectBtn);
    btnRow->addStretch();
    detailLayout->addLayout(btnRow, 13, 1);

    comfyLayout->addWidget(m_comfyDetails);
    bodyLayout->addWidget(comfyGroup);
    bodyLayout->addSpacing(24);

    // ---- Section: Facets
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
    m_quickCharFacet->setPlaceholderText("rCharacter");
    m_quickCharFacet->setText(settings->quickCharacterFacet);

    m_quickCopyFacet = new QLineEdit;
    m_quickCopyFacet->setObjectName("SettingsInput");
    m_quickCopyFacet->setPlaceholderText("rCopyright");
    m_quickCopyFacet->setText(settings->quickCopyrightFacet);

    m_quickTriggerFacet = new QLineEdit;
    m_quickTriggerFacet->setObjectName("SettingsInput");
    m_quickTriggerFacet->setPlaceholderText("rTriggerWord");
    m_quickTriggerFacet->setText(settings->quickTriggerWordFacet);

    m_quickStyleFacet = new QLineEdit;
    m_quickStyleFacet->setObjectName("SettingsInput");
    m_quickStyleFacet->setPlaceholderText("rStyle");
    m_quickStyleFacet->setText(settings->quickStyleFacet);

    auto* facetsHint =
        new QLabel("Names of facets used by the composer's right-click \"Quick add\" actions.");
    facetsHint->setObjectName("SettingsHintLabel");
    facetsHint->setWordWrap(true);

    facetsLayout->addWidget(makeLabel("Quick character facet"), 0, 0);
    facetsLayout->addWidget(m_quickCharFacet, 0, 1);
    facetsLayout->addWidget(makeLabel("Quick copyright facet"), 1, 0);
    facetsLayout->addWidget(m_quickCopyFacet, 1, 1);
    facetsLayout->addWidget(makeLabel("Quick trigger word facet"), 2, 0);
    facetsLayout->addWidget(m_quickTriggerFacet, 2, 1);
    facetsLayout->addWidget(makeLabel("Quick style facet"), 3, 0);
    facetsLayout->addWidget(m_quickStyleFacet, 3, 1);
    facetsLayout->addWidget(facetsHint, 4, 1);

    auto* purgeHint =
        new QLabel("Drop tag definitions that have zero facets, or that aren't in the Danbooru "
                   "list and aren't used by any entry. Or strip facet entries whose name isn't "
                   "in facets.fct (case-sensitive). Removed entries won't be written to "
                   "tag_definitions.fct on shutdown.");
    purgeHint->setObjectName("SettingsHintLabel");
    purgeHint->setWordWrap(true);

    auto* purgeBtn = new QPushButton("Purge stale tag definitions");
    purgeBtn->setObjectName("SettingsLaunchBtn");
    purgeBtn->setCursor(Qt::PointingHandCursor);

    auto* purgeUnknownBtn = new QPushButton("Purge unknown facets");
    purgeUnknownBtn->setObjectName("SettingsLaunchBtn");
    purgeUnknownBtn->setCursor(Qt::PointingHandCursor);

    auto* purgeRow = new QHBoxLayout;
    purgeRow->setContentsMargins(0, 0, 0, 0);
    purgeRow->setSpacing(8);
    purgeRow->addWidget(purgeBtn);
    purgeRow->addWidget(purgeUnknownBtn);
    purgeRow->addStretch();

    facetsLayout->addWidget(purgeHint, 5, 1);
    facetsLayout->addLayout(purgeRow, 6, 1);

    auto* openDanbooruBtn = new QPushButton("Open danbooru.csv");
    openDanbooruBtn->setObjectName("SettingsLaunchBtn");
    openDanbooruBtn->setCursor(Qt::PointingHandCursor);

    auto* openGroupsBtn = new QPushButton("Open groups.fct");
    openGroupsBtn->setObjectName("SettingsLaunchBtn");
    openGroupsBtn->setCursor(Qt::PointingHandCursor);

    auto* openDefinitionsBtn = new QPushButton("Open tag_definitions.fct");
    openDefinitionsBtn->setObjectName("SettingsLaunchBtn");
    openDefinitionsBtn->setCursor(Qt::PointingHandCursor);

    auto* systemFilesHint =
        new QLabel("Edit the Danbooru tag CSV, tag-group categories, or tag definitions file in "
                   "your default editor. Restart to apply changes. Note: tag_definitions.fct is "
                   "rewritten on shutdown - edit it only while the app is closed, or your changes "
                   "will be overwritten.");
    systemFilesHint->setObjectName("SettingsHintLabel");
    systemFilesHint->setWordWrap(true);

    auto* systemFilesRow = new QHBoxLayout;
    systemFilesRow->setContentsMargins(0, 0, 0, 0);
    systemFilesRow->setSpacing(8);
    systemFilesRow->addWidget(openDanbooruBtn);
    systemFilesRow->addWidget(openGroupsBtn);
    systemFilesRow->addWidget(openDefinitionsBtn);
    systemFilesRow->addStretch();

    facetsLayout->addWidget(systemFilesHint, 7, 1);
    facetsLayout->addLayout(systemFilesRow, 8, 1);

    connect(purgeBtn, &QPushButton::clicked, this, &SettingsPage::purgeTagDefinitionsRequested);
    connect(purgeUnknownBtn, &QPushButton::clicked, this,
            &SettingsPage::purgeUnknownFacetsRequested);
    connect(openDanbooruBtn, &QPushButton::clicked, this, []() {
        utils::openSystemFile(utils::BASE_PATH + "/" + utils::DANBOORU_CSV_PATH,
                              QByteArrayLiteral("tag,category,count,wrong\n"));
    });
    connect(openGroupsBtn, &QPushButton::clicked, this, []() {
        utils::openSystemFile(utils::BASE_PATH + "/" + utils::GROUPS_PATH);
    });
    connect(openDefinitionsBtn, &QPushButton::clicked, this, []() {
        utils::openSystemFile(utils::BASE_PATH + "/" + utils::DEFINITIONS_PATH);
    });

    bodyLayout->addWidget(facetsGroup);
    bodyLayout->addSpacing(24);

    // ---- Section: Data (import / export)
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

    auto* dataHint =
        new QLabel("Export selected entries (with referenced tag definitions) to a folder, "
                   "or import a previously-exported folder. Imports merge into your data; "
                   "duplicate entries are skipped, and tag-definition collisions are handled "
                   "per the option selected in the import dialog.");
    dataHint->setObjectName("SettingsHintLabel");
    dataHint->setWordWrap(true);
    dataLayout->addWidget(dataHint);

    auto* dataBtnRow = new QHBoxLayout;
    auto* exportBtn = new QPushButton("Export entries…");
    exportBtn->setObjectName("SettingsLaunchBtn");
    exportBtn->setCursor(Qt::PointingHandCursor);
    auto* importBtn = new QPushButton("Import entries…");
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

    // ---- Section: Workflow input images
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

    auto* inputsHint =
        new QLabel("Workflow image inputs, painted masks, and rendered edit variants are "
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

    connect(clearInputsBtn, &QPushButton::clicked, this, &SettingsPage::clearUnusedInputsRequested);

    bodyLayout->addWidget(inputsGroup);
    bodyLayout->addSpacing(24);

    // ---- Section: Log
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
    m_log->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_log->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));
    for (const QString& msg : utils::Logger::instance().history())
        m_log->appendPlainText(msg);
    bodyLayout->addWidget(m_log);
    bodyLayout->addSpacing(16);

    auto* footerRow = new QHBoxLayout;
    footerRow->setContentsMargins(0, 0, 0, 0);
    footerRow->setSpacing(6);
    footerRow->addStretch();

    auto* footerIcon = new QLabel;
    footerIcon->setPixmap(QPixmap(":/icons/taskbar.png")
                              .scaled(20, 20, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    footerRow->addWidget(footerIcon);

    auto* versionLabel =
        new QLabel(QString("%1 v%2").arg(utils::APP_NAME, utils::APP_VERSION));
    versionLabel->setObjectName("SettingsHintLabel");
    footerRow->addWidget(versionLabel);
    footerRow->addStretch();

    bodyLayout->addLayout(footerRow);
    bodyLayout->addStretch();

    // ---- Scroll area
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

    // ---- Connections
    connect(m_enableDanmaku, &QCheckBox::toggled, this, [this](bool on) {
        m_settings->danmakuEnabled = on;
        emit settingsChanged();
    });

    connect(m_tileGradStart, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) {
                m_settings->tileGradientStart = v;
                emit settingsChanged();
            });
    connect(m_tileGradAlpha, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        m_settings->tileGradientAlpha = v;
        emit settingsChanged();
    });

    connect(m_sfxVolume, &QSlider::valueChanged, this, [this](int v) {
        m_settings->sfxVolume = v / 100.0f;
        m_sfxVolumeValue->setText(QString::number(v) + "%");
        if (auto* sp = core::SoundPlayer::instance()) sp->setVolume(m_settings->sfxVolume);
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
        const QString raw = m_outputFolder->text().trimmed();
        const QString startDir = raw.section(QLatin1Char('{'), 0, 0).trimmed();
        const QString dir =
            QFileDialog::getExistingDirectory(this, "Select ComfyUI Output Folder", startDir);
        if (dir.isEmpty()) return;
        // Append the date-pattern suffix the user had typed, if any
        const int bracePos = raw.indexOf(QLatin1Char('{'));
        const QString newPath = bracePos >= 0 ? dir + "/" + raw.mid(bracePos) : dir;
        m_outputFolder->setText(newPath);
        m_settings->comfyUiOutputFolder = newPath;
        emit settingsChanged();
    });

    connect(m_tempFolder, &QLineEdit::editingFinished, this, [this]() {
        m_settings->comfyUiTempFolder = m_tempFolder->text().trimmed();
        emit settingsChanged();
    });

    connect(tempBrowseBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, "Select ComfyUI Temp Folder",
                                                              m_tempFolder->text().trimmed());
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
        const QString dir = QFileDialog::getExistingDirectory(this, "Select LoRA Base Folder",
                                                              m_loraBaseDir->text().trimmed());
        if (dir.isEmpty()) return;
        m_loraBaseDir->setText(dir);
        m_settings->loraBaseDir = dir;
        emit settingsChanged();
    });

    connect(m_loraTestDir, &QLineEdit::editingFinished, this, [this]() {
        m_settings->loraTestDir = m_loraTestDir->text().trimmed();
        emit settingsChanged();
    });

    connect(loraTestBrowseBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, "Select LoRA Test Folder",
                                                              m_loraTestDir->text().trimmed());
        if (dir.isEmpty()) return;
        m_loraTestDir->setText(dir);
        m_settings->loraTestDir = dir;
        emit settingsChanged();
    });

    connect(m_defaultLoraModelStr, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) {
                m_settings->defaultLoraModelStr = v;
                emit settingsChanged();
            });
    connect(m_defaultLoraClipStr, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) {
                m_settings->defaultLoraClipStr = v;
                emit settingsChanged();
            });

    connect(m_inputFolder, &QLineEdit::editingFinished, this, [this]() {
        m_settings->comfyUiInputFolder = m_inputFolder->text().trimmed();
        emit settingsChanged();
    });

    connect(inputBrowseBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, "Select ComfyUI Input Folder",
                                                              m_inputFolder->text().trimmed());
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

    connect(connectBtn, &QPushButton::clicked, this, &SettingsPage::reconnectRequested);

    connect(&utils::Logger::instance(), &utils::Logger::messageLogged, this,
            &SettingsPage::appendLogMessage);

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
        m_statusDot->setProperty("status", "disabled");
        m_statusText->setText("disabled");
    }
    else {
        m_statusDot->setProperty("status", "connecting");
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
        m_statusDot->setProperty("status", "error");
        m_statusText->setText(error);
    }
    else if (connected) {
        m_statusDot->setProperty("status", "connected");
        m_statusText->setText("connected");
    }
    else {
        m_statusDot->setProperty("status", "disconnected");
        m_statusText->setText("disconnected");
    }
    m_statusDot->style()->unpolish(m_statusDot);
    m_statusDot->style()->polish(m_statusDot);
}

} // namespace gui
