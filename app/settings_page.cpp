#include <app/settings_page.h>
#include <app/app_data.h>
#include <app/app_scroll_bar.h>
#include <app/chromed_dialog.h>
#include <app/icons.h>
#include <app/logger.h>
#include <app/paths.h>
#include <app/widget_utils.h>
#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

QLabel* fieldLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(u"SettingsFieldLabel"_s);
    return label;
}

QLabel* hintLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(u"SettingsHintLabel"_s);
    label->setWordWrap(true);
    return label;
}

QLabel* sectionHeader(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(u"SettingsSectionHeader"_s);
    return label;
}

QWidget* settingsGroup(QLayout** layoutOut, bool grid)
{
    auto* group = new QWidget;
    group->setObjectName(u"SettingsGroup"_s);
    group->setAttribute(Qt::WA_StyledBackground, true);

    if (grid) {
        auto* layout = new QGridLayout(group);
        layout->setContentsMargins(16, 14, 16, 14);
        layout->setHorizontalSpacing(12);
        layout->setVerticalSpacing(10);
        layout->setColumnStretch(1, 1);
        *layoutOut = layout;
    } else {
        auto* layout = new QVBoxLayout(group);
        layout->setContentsMargins(16, 14, 16, 14);
        layout->setSpacing(8);
        *layoutOut = layout;
    }
    return group;
}

QLineEdit* settingsInput(const QString& placeholder, const QString& value)
{
    auto* edit = new QLineEdit;
    edit->setObjectName(u"SettingsInput"_s);
    edit->setPlaceholderText(placeholder);
    edit->setText(value);
    return edit;
}

QPushButton* browseButton()
{
    auto* button = new QPushButton(u"Browse"_s);
    button->setObjectName(u"SettingsBrowseBtn"_s);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedWidth(70);
    return button;
}

QPushButton* launchButton(const QString& text)
{
    auto* button = new QPushButton(text);
    button->setObjectName(u"SettingsLaunchBtn"_s);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

// A field plus its Browse button on one line.
QHBoxLayout* browseRow(QLineEdit* edit, QPushButton* browse)
{
    auto* row = new QHBoxLayout;
    row->setSpacing(6);
    row->addWidget(edit, 1);
    row->addWidget(browse);
    return row;
}

QHBoxLayout* leftAligned(QWidget* widget)
{
    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(widget);
    row->addStretch();
    return row;
}

} // namespace

SettingsPage::SettingsPage(Settings& settings, AppData& data, QWidget* parent)
    : QWidget(parent), m_settings(&settings), m_data(&data)
{
    setObjectName(u"SettingsPage"_s);
    setAttribute(Qt::WA_StyledBackground, true);

    auto* body = new QWidget;
    body->setObjectName(u"SettingsBody"_s);

    auto* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(32, 24, 32, 12);
    bodyLayout->setSpacing(0);

    // ---- Appearance
    bodyLayout->addWidget(sectionHeader(u"APPEARANCE"_s));
    bodyLayout->addSpacing(12);

    QLayout* appearanceBase = nullptr;
    QWidget* appearanceGroup = settingsGroup(&appearanceBase, false);
    auto* appearanceLayout = static_cast<QVBoxLayout*>(appearanceBase);

    auto* tileGrid = new QGridLayout;
    tileGrid->setContentsMargins(0, 6, 0, 0);
    tileGrid->setHorizontalSpacing(12);
    tileGrid->setVerticalSpacing(8);
    tileGrid->setColumnStretch(1, 1);

    m_tileGradStart = new QDoubleSpinBox;
    m_tileGradStart->setObjectName(u"SettingsInput"_s);
    m_tileGradStart->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_tileGradStart->setRange(0.0, 1.0);
    m_tileGradStart->setDecimals(2);
    m_tileGradStart->setSingleStep(0.05);
    m_tileGradStart->setValue(settings.tileGradientStart);
    m_tileGradStart->setFixedWidth(80);

    m_tileGradAlpha = new QSpinBox;
    m_tileGradAlpha->setObjectName(u"SettingsInput"_s);
    m_tileGradAlpha->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_tileGradAlpha->setRange(0, 255);
    m_tileGradAlpha->setValue(settings.tileGradientAlpha);
    m_tileGradAlpha->setFixedWidth(80);

    m_tileTitleColor = new QPushButton;
    m_tileTitleColor->setFixedSize(110, 32);
    m_tileTitleColor->setCursor(Qt::PointingHandCursor);

    // Inline style: the swatch shows the chosen color.
    auto applyTitleSwatch = [this]() {
        const QColor colour(m_settings->tileTitleColor);
        const bool dark = colour.isValid()
            && (0.299 * colour.red() + 0.587 * colour.green() + 0.114 * colour.blue()) < 128;
        m_tileTitleColor->setStyleSheet(
            u"background-color: %1; color: %2; border: 1px solid #2a2a2a; "
            u"border-radius: 3px; font-family: monospace; font-size: 12px;"_s
                .arg(m_settings->tileTitleColor, dark ? u"#ffffff"_s : u"#000000"_s));
        m_tileTitleColor->setText(m_settings->tileTitleColor);
    };
    applyTitleSwatch();

    connect(m_tileTitleColor, &QPushButton::clicked, this, [this, applyTitleSwatch]() {
        const QColor initial(m_settings->tileTitleColor);

        ChromedDialog wrapper(this);
        wrapper.setWindowTitle(u"Tile title color"_s);

        auto* picker =
            new QColorDialog(initial.isValid() ? initial : Qt::white, wrapper.contentArea());
        picker->setOptions(QColorDialog::DontUseNativeDialog | QColorDialog::NoButtons);
        picker->setWindowFlags(Qt::Widget); // embedded, not a separate window
        picker->setSizeGripEnabled(false);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                             wrapper.contentArea());
        connect(buttons, &QDialogButtonBox::accepted, &wrapper, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &wrapper, &QDialog::reject);
        connect(picker, &QColorDialog::colorSelected, &wrapper,
                [&wrapper](const QColor&) { wrapper.accept(); });
        connect(picker, &QDialog::rejected, &wrapper, &QDialog::reject);

        auto* layout = new QVBoxLayout(wrapper.contentArea());
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        layout->addWidget(picker, 1);
        layout->addWidget(buttons);

        if (wrapper.exec() != QDialog::Accepted) return;
        const QColor chosen = picker->currentColor();
        if (!chosen.isValid()) return;
        m_settings->tileTitleColor = chosen.name();
        applyTitleSwatch();
        emit settingsChanged();
    });

    tileGrid->addWidget(fieldLabel(u"Tile gradient start (0-1)"_s), 0, 0);
    tileGrid->addLayout(leftAligned(m_tileGradStart), 0, 1);
    tileGrid->addWidget(fieldLabel(u"Tile gradient opacity (0-255)"_s), 1, 0);
    tileGrid->addLayout(leftAligned(m_tileGradAlpha), 1, 1);
    tileGrid->addWidget(fieldLabel(u"Tile title color"_s), 2, 0);
    tileGrid->addLayout(leftAligned(m_tileTitleColor), 2, 1);

    appearanceLayout->addLayout(tileGrid);
    appearanceLayout->addWidget(hintLabel(
        u"Bottom fade and title text behind tile thumbnails. Restart to apply."_s));

    m_sfxVolume = new QSlider(Qt::Horizontal);
    m_sfxVolume->setObjectName(u"DatasetPmiSlider"_s);
    m_sfxVolume->setRange(0, 100);
    m_sfxVolume->setValue(int(qBound(0.0f, settings.sfxVolume, 1.0f) * 100.0f));

    m_sfxVolumeValue = new QLabel;
    m_sfxVolumeValue->setObjectName(u"SettingsFieldLabel"_s);
    m_sfxVolumeValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_sfxVolumeValue->setMinimumWidth(36);
    m_sfxVolumeValue->setText(QString::number(m_sfxVolume->value()) + u"%"_s);

    auto* sfxRow = new QHBoxLayout;
    sfxRow->setContentsMargins(0, 8, 0, 0);
    sfxRow->setSpacing(12);
    sfxRow->addWidget(fieldLabel(u"Sound effect volume"_s));
    sfxRow->addWidget(m_sfxVolume, 1);
    sfxRow->addWidget(m_sfxVolumeValue);
    appearanceLayout->addLayout(sfxRow);

    bodyLayout->addWidget(appearanceGroup);
    bodyLayout->addSpacing(24);

    // ---- Backends
    bodyLayout->addWidget(sectionHeader(u"BACKENDS"_s));
    bodyLayout->addSpacing(12);

    auto* comfyGroup = new QWidget;
    comfyGroup->setObjectName(u"SettingsGroup"_s);
    comfyGroup->setAttribute(Qt::WA_StyledBackground, true);
    auto* comfyLayout = new QVBoxLayout(comfyGroup);
    comfyLayout->setContentsMargins(16, 14, 16, 14);
    comfyLayout->setSpacing(12);

    auto* toggleRow = new QHBoxLayout;
    toggleRow->setSpacing(10);

    m_enableComfy = new QCheckBox(u"ComfyUI"_s);
    m_enableComfy->setObjectName(u"SettingsCheckBox"_s);
    m_enableComfy->setChecked(settings.comfyEnabled);
    toggleRow->addWidget(m_enableComfy);
    toggleRow->addStretch();

    m_statusDot = new QLabel; // a disc drawn by the stylesheet
    m_statusDot->setObjectName(u"SettingsStatusDot"_s);
    m_statusText = new QLabel(u"disabled"_s);
    m_statusText->setObjectName(u"SettingsStatusText"_s);
    toggleRow->addWidget(m_statusDot);
    toggleRow->addWidget(m_statusText);
    comfyLayout->addLayout(toggleRow);

    m_comfyDetails = new QWidget;
    auto* detail = new QGridLayout(m_comfyDetails);
    detail->setContentsMargins(0, 4, 0, 0);
    detail->setHorizontalSpacing(12);
    detail->setVerticalSpacing(10);
    detail->setColumnStretch(1, 1);

    m_serverAddress = settingsInput(u"127.0.0.1:8188"_s, settings.comfyServerAddress);

    m_apiKey = settingsInput(u"API key (leave blank for local)"_s, settings.comfyApiKey);
    m_apiKey->setEchoMode(QLineEdit::Password);

    m_outputFolder =
        settingsInput(u"e.g. C:/ComfyUI/output/{yyyy-MM-dd}"_s, settings.comfyOutputFolder);
    QPushButton* outputBrowse = browseButton();

    m_tempFolder = settingsInput(u"e.g. C:/ComfyUI/temp"_s, settings.comfyTempFolder);
    QPushButton* tempBrowse = browseButton();

    m_loraBaseDir = settingsInput(u"e.g. C:/ComfyUI/models/loras"_s, settings.loraBaseDir);
    QPushButton* loraBrowse = browseButton();

    m_loraTestDir = settingsInput(
        u"Optional - secondary lora folder for testing (e.g. C:/Users/.../Downloads)"_s,
        settings.loraTestDir);
    QPushButton* loraTestBrowse = browseButton();

    auto makeLoraSpin = [](double min, double max, double step, double value) {
        auto* spin = new QDoubleSpinBox;
        spin->setObjectName(u"LoraSpinBox"_s);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setRange(min, max);
        spin->setSingleStep(step);
        spin->setDecimals(2);
        spin->setValue(value);
        spin->setFixedWidth(48);
        return spin;
    };
    auto loraSpinLabel = [](const QString& text) {
        auto* label = new QLabel(text);
        label->setObjectName(u"LoraSpinLabel"_s);
        return label;
    };

    m_defaultLoraModelStrength = makeLoraSpin(0.0, 2.0, 0.05, settings.defaultLoraModelStrength);
    m_defaultLoraClipStrength = makeLoraSpin(0.0, 4.0, 0.10, settings.defaultLoraClipStrength);

    auto* loraDefaultsRow = new QHBoxLayout;
    loraDefaultsRow->setContentsMargins(0, 0, 0, 0);
    loraDefaultsRow->setSpacing(6);
    loraDefaultsRow->addWidget(loraSpinLabel(u"Model"_s));
    loraDefaultsRow->addWidget(m_defaultLoraModelStrength);
    loraDefaultsRow->addSpacing(8);
    loraDefaultsRow->addWidget(loraSpinLabel(u"Clip"_s));
    loraDefaultsRow->addWidget(m_defaultLoraClipStrength);
    loraDefaultsRow->addStretch();

    m_inputFolder = settingsInput(
        u"e.g. C:/ComfyUI/input  (optional - enables direct file copy)"_s,
        settings.comfyInputFolder);
    QPushButton* inputBrowse = browseButton();

    auto* connectBtn = new QPushButton(u"Connect"_s);
    connectBtn->setObjectName(u"SettingsConnectBtn"_s);
    connectBtn->setCursor(Qt::PointingHandCursor);

    auto* separator = new QFrame;
    separator->setFrameShape(QFrame::HLine);
    separator->setObjectName(u"SettingsSeparator"_s);

    detail->addWidget(separator, 0, 0, 1, 2);
    detail->addWidget(fieldLabel(u"Server address"_s), 1, 0);
    detail->addWidget(m_serverAddress, 1, 1);
    detail->addWidget(fieldLabel(u"API key"_s), 2, 0);
    detail->addWidget(m_apiKey, 2, 1);
    detail->addWidget(fieldLabel(u"Output folder"_s), 3, 0);
    detail->addLayout(browseRow(m_outputFolder, outputBrowse), 3, 1);
    detail->addWidget(hintLabel(u"Use {yyyy-MM-dd} for date-based subfolders"_s), 4, 1);
    detail->addWidget(fieldLabel(u"Temp folder"_s), 5, 0);
    detail->addLayout(browseRow(m_tempFolder, tempBrowse), 5, 1);
    detail->addWidget(fieldLabel(u"LoRA folder"_s), 6, 0);
    detail->addLayout(browseRow(m_loraBaseDir, loraBrowse), 6, 1);
    detail->addWidget(fieldLabel(u"LoRA test folder"_s), 7, 0);
    detail->addLayout(browseRow(m_loraTestDir, loraTestBrowse), 7, 1);
    detail->addWidget(
        hintLabel(u"Files dropped from this folder are recognised as already-placed and skip "
                  u"the import dialog. Match this to ComfyUI's extra_model_paths.yaml entry."_s),
        8, 1);
    detail->addWidget(fieldLabel(u"LoRA defaults"_s), 9, 0);
    detail->addLayout(loraDefaultsRow, 9, 1);
    detail->addWidget(
        hintLabel(u"Applied when adding a new LoRA. Existing entries keep their values."_s), 10,
        1);
    detail->addWidget(fieldLabel(u"Input folder"_s), 11, 0);
    detail->addLayout(browseRow(m_inputFolder, inputBrowse), 11, 1);
    detail->addWidget(
        hintLabel(u"Image-typed workflow vars upload to ComfyUI on each run. Set this to "
                  u"ComfyUI's input/ folder to skip HTTP and copy directly."_s),
        12, 1);

    auto* connectRow = new QHBoxLayout;
    connectRow->setSpacing(8);
    connectRow->addWidget(connectBtn);
    connectRow->addStretch();
    detail->addLayout(connectRow, 13, 1);

    comfyLayout->addWidget(m_comfyDetails);
    bodyLayout->addWidget(comfyGroup);
    bodyLayout->addSpacing(24);

    // ---- Facets
    bodyLayout->addWidget(sectionHeader(u"FACETS"_s));
    bodyLayout->addSpacing(12);

    QLayout* facetsBase = nullptr;
    QWidget* facetsGroup = settingsGroup(&facetsBase, true);
    auto* facetsLayout = static_cast<QGridLayout*>(facetsBase);

    m_quickCharFacet = settingsInput(u"rCharacter"_s, settings.quickCharacterFacet);
    m_quickCopyFacet = settingsInput(u"rCopyright"_s, settings.quickCopyrightFacet);
    m_quickTriggerFacet = settingsInput(u"rTriggerWord"_s, settings.quickTriggerWordFacet);
    m_quickStyleFacet = settingsInput(u"rStyle"_s, settings.quickStyleFacet);

    facetsLayout->addWidget(fieldLabel(u"Quick character facet"_s), 0, 0);
    facetsLayout->addWidget(m_quickCharFacet, 0, 1);
    facetsLayout->addWidget(fieldLabel(u"Quick copyright facet"_s), 1, 0);
    facetsLayout->addWidget(m_quickCopyFacet, 1, 1);
    facetsLayout->addWidget(fieldLabel(u"Quick trigger word facet"_s), 2, 0);
    facetsLayout->addWidget(m_quickTriggerFacet, 2, 1);
    facetsLayout->addWidget(fieldLabel(u"Quick style facet"_s), 3, 0);
    facetsLayout->addWidget(m_quickStyleFacet, 3, 1);
    facetsLayout->addWidget(
        hintLabel(
            u"Names of facets used by the composer's right-click \"Quick add\" actions."_s),
        4, 1);

    m_formatsContainer = new QWidget;
    auto* formatsLayout = new QVBoxLayout(m_formatsContainer);
    formatsLayout->setContentsMargins(0, 0, 0, 0);
    formatsLayout->setSpacing(4);

    facetsLayout->addWidget(fieldLabel(u"Tag formatting"_s), 5, 0, Qt::AlignTop);
    facetsLayout->addWidget(m_formatsContainer, 5, 1);
    facetsLayout->addWidget(
        hintLabel(u"Wraps every tag whose facets include the listed facet with prefix + tag + "
                  u"suffix right before the prompt is built for copy/ComfyUI. Rules stack in "
                  u"order. Example: facet=rStyle, prefix=@, suffix=(empty) emits \"@asanagi\". "
                  u"This list is the fallback: a format profile selected in the composer's "
                  u"PROFILES section (data/system/profiles.fct) overrides it."_s),
        6, 1);
    rebuildFacetFormats();

    QPushButton* purgeBtn = launchButton(u"Purge stale tag definitions"_s);
    QPushButton* purgeUnknownBtn = launchButton(u"Purge unknown facets"_s);

    auto* purgeRow = new QHBoxLayout;
    purgeRow->setContentsMargins(0, 0, 0, 0);
    purgeRow->setSpacing(8);
    purgeRow->addWidget(purgeBtn);
    purgeRow->addWidget(purgeUnknownBtn);
    purgeRow->addStretch();

    facetsLayout->addLayout(purgeRow, 7, 1);
    facetsLayout->addWidget(
        hintLabel(u"Drop tag definitions that have zero facets, or that aren't in the Danbooru "
                  u"list and aren't used by any entry. Or strip facet entries whose name isn't "
                  u"in facets.fct (case-sensitive). Removed entries won't be written to "
                  u"tag_definitions.fct on shutdown."_s),
        8, 1);

    QPushButton* openDanbooruBtn = launchButton(u"Open danbooru.csv"_s);
    QPushButton* openGroupsBtn = launchButton(u"Open groups.fct"_s);
    QPushButton* openDefinitionsBtn = launchButton(u"Open tag_definitions.fct"_s);

    auto* systemFilesRow = new QHBoxLayout;
    systemFilesRow->setContentsMargins(0, 0, 0, 0);
    systemFilesRow->setSpacing(8);
    systemFilesRow->addWidget(openDanbooruBtn);
    systemFilesRow->addWidget(openGroupsBtn);
    systemFilesRow->addWidget(openDefinitionsBtn);
    systemFilesRow->addStretch();

    facetsLayout->addLayout(systemFilesRow, 9, 1);
    facetsLayout->addWidget(
        hintLabel(u"Edit the Danbooru tag CSV, tag-group categories, or tag definitions file in "
                  u"your default editor. Restart to apply changes. Note: tag_definitions.fct is "
                  u"rewritten on shutdown - edit it only while the app is closed, or your "
                  u"changes will be overwritten."_s),
        10, 1);

    connect(purgeBtn, &QPushButton::clicked, this, &SettingsPage::purgeTagDefinitionsRequested);
    connect(purgeUnknownBtn, &QPushButton::clicked, this,
            &SettingsPage::purgeUnknownFacetsRequested);
    connect(openDanbooruBtn, &QPushButton::clicked, this, [this]() {
        openSystemFile(m_data->dataPath(paths::kDanbooruCsv),
                       QByteArrayLiteral("tag,category,count,wrong\n"));
    });
    connect(openGroupsBtn, &QPushButton::clicked, this,
            [this]() { openSystemFile(m_data->dataPath(paths::kGroups)); });
    connect(openDefinitionsBtn, &QPushButton::clicked, this,
            [this]() { openSystemFile(m_data->dataPath(paths::kDefinitions)); });

    bodyLayout->addWidget(facetsGroup);
    bodyLayout->addSpacing(24);

    // ---- Prompt composer
    bodyLayout->addWidget(sectionHeader(u"PROMPT COMPOSER"_s));
    bodyLayout->addSpacing(12);

    QLayout* composerBase = nullptr;
    QWidget* composerGroup = settingsGroup(&composerBase, false);
    auto* composerLayout = static_cast<QVBoxLayout*>(composerBase);

    m_forceOverwriteRules =
        new QCheckBox(u"Force overwrite rules when loading a saved state"_s);
    m_forceOverwriteRules->setObjectName(u"SettingsCheckBox"_s);
    m_forceOverwriteRules->setChecked(settings.forceOverwriteRulesOnStateLoad);
    composerLayout->addWidget(m_forceOverwriteRules);
    composerLayout->addWidget(
        hintLabel(u"When loading a state, replace the match expression, action type, and "
                  u"force-fire flag of any rule already in memory (same id) with the saved "
                  u"version. Off keeps those fields local. The enabled flag and Add/Replace "
                  u"injected tags are always refreshed from the state regardless. Rule names "
                  u"are never overwritten."_s));

    bodyLayout->addWidget(composerGroup);
    bodyLayout->addSpacing(24);

    // ---- Data
    bodyLayout->addWidget(sectionHeader(u"DATA"_s));
    bodyLayout->addSpacing(12);

    QLayout* dataBase = nullptr;
    QWidget* dataGroup = settingsGroup(&dataBase, false);
    auto* dataLayout = static_cast<QVBoxLayout*>(dataBase);
    dataLayout->setSpacing(10);

    dataLayout->addWidget(
        hintLabel(u"Export selected entries (with referenced tag definitions) to a folder, or "
                  u"import a previously-exported folder. Imports merge into your data; "
                  u"duplicate entries are skipped, and tag-definition collisions are handled "
                  u"per the option selected in the import dialog."_s));

    QPushButton* exportBtn = launchButton(QString::fromUtf8("Export entries\xE2\x80\xA6"));
    QPushButton* importBtn = launchButton(QString::fromUtf8("Import entries\xE2\x80\xA6"));

    auto* dataBtnRow = new QHBoxLayout;
    dataBtnRow->addWidget(exportBtn);
    dataBtnRow->addWidget(importBtn);
    dataBtnRow->addStretch();
    dataLayout->addLayout(dataBtnRow);

    connect(exportBtn, &QPushButton::clicked, this, &SettingsPage::exportEntriesRequested);
    connect(importBtn, &QPushButton::clicked, this, &SettingsPage::importEntriesRequested);

    bodyLayout->addWidget(dataGroup);
    bodyLayout->addSpacing(24);

    // ---- Input images
    bodyLayout->addWidget(sectionHeader(u"INPUT IMAGES"_s));
    bodyLayout->addSpacing(12);

    QLayout* inputsBase = nullptr;
    QWidget* inputsGroup = settingsGroup(&inputsBase, false);
    auto* inputsLayout = static_cast<QVBoxLayout*>(inputsBase);
    inputsLayout->setSpacing(10);

    inputsLayout->addWidget(
        hintLabel(u"Workflow image inputs, painted masks, and rendered edit variants are cached "
                  u"on disk. This removes any cache entry that no current workflow variable "
                  u"references - useful after deleting workflows or replacing image inputs."_s));

    QPushButton* clearInputsBtn = launchButton(u"Clear unused inputs"_s);
    auto* inputsBtnRow = new QHBoxLayout;
    inputsBtnRow->addWidget(clearInputsBtn);
    inputsBtnRow->addStretch();
    inputsLayout->addLayout(inputsBtnRow);

    connect(clearInputsBtn, &QPushButton::clicked, this,
            &SettingsPage::clearUnusedInputsRequested);

    bodyLayout->addWidget(inputsGroup);
    bodyLayout->addSpacing(24);

    // ---- Log
    bodyLayout->addWidget(sectionHeader(u"LOG"_s));
    bodyLayout->addSpacing(12);

    m_log = new QPlainTextEdit;
    m_log->setObjectName(u"LogView"_s);
    m_log->setReadOnly(true);
    m_log->setUndoRedoEnabled(false);
    m_log->setMaximumBlockCount(5000);
    m_log->setMinimumHeight(200);
    m_log->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_log->setVerticalScrollBar(new AppScrollBar(Qt::Vertical));
    for (const QString& message : Logger::instance().history())
        m_log->appendPlainText(message);

    bodyLayout->addWidget(m_log);
    bodyLayout->addSpacing(16);

    auto* footerRow = new QHBoxLayout;
    footerRow->setContentsMargins(0, 0, 0, 0);
    footerRow->setSpacing(6);
    footerRow->addStretch();

    auto* footerIcon = new QLabel;
    footerIcon->setPixmap(QPixmap(u":/icons/taskbar.png"_s)
                              .scaled(20, 20, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    footerRow->addWidget(footerIcon);

    auto* versionLabel = new QLabel(u"%1 v%2"_s.arg(QString::fromLatin1(kAppName),
                                                    QString::fromLatin1(kAppVersion)));
    versionLabel->setObjectName(u"SettingsHintLabel"_s);
    footerRow->addWidget(versionLabel);
    footerRow->addStretch();

    bodyLayout->addLayout(footerRow);
    bodyLayout->addStretch();

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(u"SettingsScroll"_s);
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
    connect(m_tileGradStart, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        m_settings->tileGradientStart = value;
        emit settingsChanged();
    });
    connect(m_tileGradAlpha, &QSpinBox::valueChanged, this, [this](int value) {
        m_settings->tileGradientAlpha = value;
        emit settingsChanged();
    });
    connect(m_sfxVolume, &QSlider::valueChanged, this, [this](int value) {
        m_settings->sfxVolume = value / 100.0f;
        m_sfxVolumeValue->setText(QString::number(value) + u"%"_s);
        emit settingsChanged();
    });

    connect(m_enableComfy, &QCheckBox::toggled, this, &SettingsPage::onComfyToggled);

    connect(m_serverAddress, &QLineEdit::editingFinished, this, [this]() {
        m_settings->comfyServerAddress = m_serverAddress->text().trimmed();
        if (m_settings->comfyServerAddress.isEmpty())
            m_settings->comfyServerAddress = u"127.0.0.1:8188"_s;
        emit settingsChanged();
    });
    connect(m_apiKey, &QLineEdit::editingFinished, this, [this]() {
        m_settings->comfyApiKey = m_apiKey->text().trimmed();
        emit settingsChanged();
    });
    connect(m_outputFolder, &QLineEdit::editingFinished, this, [this]() {
        m_settings->comfyOutputFolder = m_outputFolder->text().trimmed();
        emit settingsChanged();
    });
    connect(outputBrowse, &QPushButton::clicked, this, [this]() {
        // Browse from the part before a {date} token, then re-append the token.
        const QString raw = m_outputFolder->text().trimmed();
        const QString startDir = raw.section(u'{', 0, 0).trimmed();
        const QString dir = QFileDialog::getExistingDirectory(
            this, u"Select ComfyUI Output Folder"_s, startDir);
        if (dir.isEmpty()) return;

        const qsizetype brace = raw.indexOf(u'{');
        const QString chosen = brace >= 0 ? dir + u"/"_s + raw.sliced(brace) : dir;
        m_outputFolder->setText(chosen);
        m_settings->comfyOutputFolder = chosen;
        emit settingsChanged();
    });

    connect(m_tempFolder, &QLineEdit::editingFinished, this, [this]() {
        m_settings->comfyTempFolder = m_tempFolder->text().trimmed();
        emit settingsChanged();
    });
    connect(tempBrowse, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, u"Select ComfyUI Temp Folder"_s, m_tempFolder->text().trimmed());
        if (dir.isEmpty()) return;
        m_tempFolder->setText(dir);
        m_settings->comfyTempFolder = dir;
        emit settingsChanged();
    });

    connect(m_loraBaseDir, &QLineEdit::editingFinished, this, [this]() {
        m_settings->loraBaseDir = m_loraBaseDir->text().trimmed();
        emit settingsChanged();
    });
    connect(loraBrowse, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, u"Select LoRA Base Folder"_s, m_loraBaseDir->text().trimmed());
        if (dir.isEmpty()) return;
        m_loraBaseDir->setText(dir);
        m_settings->loraBaseDir = dir;
        emit settingsChanged();
    });

    connect(m_loraTestDir, &QLineEdit::editingFinished, this, [this]() {
        m_settings->loraTestDir = m_loraTestDir->text().trimmed();
        emit settingsChanged();
    });
    connect(loraTestBrowse, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, u"Select LoRA Test Folder"_s, m_loraTestDir->text().trimmed());
        if (dir.isEmpty()) return;
        m_loraTestDir->setText(dir);
        m_settings->loraTestDir = dir;
        emit settingsChanged();
    });

    connect(m_defaultLoraModelStrength, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        m_settings->defaultLoraModelStrength = v;
        emit settingsChanged();
    });
    connect(m_defaultLoraClipStrength, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        m_settings->defaultLoraClipStrength = v;
        emit settingsChanged();
    });

    connect(m_inputFolder, &QLineEdit::editingFinished, this, [this]() {
        m_settings->comfyInputFolder = m_inputFolder->text().trimmed();
        emit settingsChanged();
    });
    connect(inputBrowse, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, u"Select ComfyUI Input Folder"_s, m_inputFolder->text().trimmed());
        if (dir.isEmpty()) return;
        m_inputFolder->setText(dir);
        m_settings->comfyInputFolder = dir;
        emit settingsChanged();
    });

    // Empty hides the menu item; the placeholder is only a suggestion.
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

    connect(m_forceOverwriteRules, &QCheckBox::toggled, this, [this](bool on) {
        m_settings->forceOverwriteRulesOnStateLoad = on;
        emit settingsChanged();
    });

    connect(connectBtn, &QPushButton::clicked, this, &SettingsPage::reconnectRequested);
    connect(&Logger::instance(), &Logger::messageLogged, this, &SettingsPage::appendLogMessage);

    onComfyToggled(settings.comfyEnabled);
}

void SettingsPage::reload()
{
    const Settings& s = *m_settings;

    // Blocked so loading doesn't write back and emit settingsChanged.
    const QSignalBlocker b2(m_tileGradStart);
    const QSignalBlocker b3(m_tileGradAlpha);
    const QSignalBlocker b4(m_sfxVolume);
    const QSignalBlocker b5(m_enableComfy);
    const QSignalBlocker b6(m_serverAddress);
    const QSignalBlocker b7(m_apiKey);
    const QSignalBlocker b8(m_outputFolder);
    const QSignalBlocker b9(m_tempFolder);
    const QSignalBlocker b10(m_loraBaseDir);
    const QSignalBlocker b11(m_loraTestDir);
    const QSignalBlocker b12(m_defaultLoraModelStrength);
    const QSignalBlocker b13(m_defaultLoraClipStrength);
    const QSignalBlocker b14(m_inputFolder);
    const QSignalBlocker b15(m_quickCharFacet);
    const QSignalBlocker b16(m_quickCopyFacet);
    const QSignalBlocker b17(m_quickTriggerFacet);
    const QSignalBlocker b18(m_quickStyleFacet);
    const QSignalBlocker b19(m_forceOverwriteRules);

    m_tileGradStart->setValue(s.tileGradientStart);
    m_tileGradAlpha->setValue(s.tileGradientAlpha);
    m_sfxVolume->setValue(int(qBound(0.0f, s.sfxVolume, 1.0f) * 100.0f));
    m_sfxVolumeValue->setText(QString::number(m_sfxVolume->value()) + u"%"_s);

    const QColor colour(s.tileTitleColor);
    const bool dark = colour.isValid()
        && (0.299 * colour.red() + 0.587 * colour.green() + 0.114 * colour.blue()) < 128;
    m_tileTitleColor->setStyleSheet(
        u"background-color: %1; color: %2; border: 1px solid #2a2a2a; border-radius: 3px; "
        u"font-family: monospace; font-size: 12px;"_s
            .arg(s.tileTitleColor, dark ? u"#ffffff"_s : u"#000000"_s));
    m_tileTitleColor->setText(s.tileTitleColor);

    m_enableComfy->setChecked(s.comfyEnabled);
    m_serverAddress->setText(s.comfyServerAddress);
    m_apiKey->setText(s.comfyApiKey);
    m_outputFolder->setText(s.comfyOutputFolder);
    m_tempFolder->setText(s.comfyTempFolder);
    m_loraBaseDir->setText(s.loraBaseDir);
    m_loraTestDir->setText(s.loraTestDir);
    m_defaultLoraModelStrength->setValue(s.defaultLoraModelStrength);
    m_defaultLoraClipStrength->setValue(s.defaultLoraClipStrength);
    m_inputFolder->setText(s.comfyInputFolder);
    m_comfyDetails->setVisible(s.comfyEnabled);

    m_quickCharFacet->setText(s.quickCharacterFacet);
    m_quickCopyFacet->setText(s.quickCopyrightFacet);
    m_quickTriggerFacet->setText(s.quickTriggerWordFacet);
    m_quickStyleFacet->setText(s.quickStyleFacet);
    m_forceOverwriteRules->setChecked(s.forceOverwriteRulesOnStateLoad);

    rebuildFacetFormats();

    m_statusDot->setProperty("status", s.comfyEnabled ? "connecting" : "disabled");
    m_statusText->setText(s.comfyEnabled ? QString::fromUtf8("connecting\xE2\x80\xA6")
                                         : u"disabled"_s);
    m_statusDot->style()->unpolish(m_statusDot);
    m_statusDot->style()->polish(m_statusDot);
}

void SettingsPage::appendLogMessage(const QString& message)
{
    m_log->appendPlainText(message);
    m_log->ensureCursorVisible();
}

void SettingsPage::onComfyToggled(bool enabled)
{
    m_settings->comfyEnabled = enabled;
    m_comfyDetails->setVisible(enabled);

    if (enabled) {
        m_statusDot->setProperty("status", "connecting");
        m_statusText->setText(QString::fromUtf8("connecting\xE2\x80\xA6"));
    } else {
        m_statusDot->setProperty("status", "disabled");
        m_statusText->setText(u"disabled"_s);
    }

    // Re-polish so the QSS picks up the property.
    m_statusDot->style()->unpolish(m_statusDot);
    m_statusDot->style()->polish(m_statusDot);

    emit settingsChanged();
}

void SettingsPage::setComfyStatus(bool connected, const QString& error)
{
    if (!m_settings->comfyEnabled) return;

    if (!error.isEmpty()) {
        m_statusDot->setProperty("status", "error");
        m_statusText->setText(error);
    } else if (connected) {
        m_statusDot->setProperty("status", "connected");
        m_statusText->setText(u"connected"_s);
    } else {
        m_statusDot->setProperty("status", "disconnected");
        m_statusText->setText(u"disconnected"_s);
    }

    m_statusDot->style()->unpolish(m_statusDot);
    m_statusDot->style()->polish(m_statusDot);
}

void SettingsPage::rebuildFacetFormats()
{
    QLayout* layout = m_formatsContainer->layout();
    while (layout->count() > 0) {
        QLayoutItem* item = layout->takeAt(0);
        if (QWidget* widget = item->widget()) widget->deleteLater();
        delete item;
    }

    for (int i = 0; i < int(m_settings->facetFormats.size()); ++i)
        layout->addWidget(makeFacetFormatRow(i, false));
    layout->addWidget(makeFacetFormatRow(-1, true));
}

QWidget* SettingsPage::makeFacetFormatRow(int index, bool isAddRow)
{
    auto* row = new QWidget;
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    QLineEdit* facetEdit =
        settingsInput(isAddRow ? u"add facet..."_s : u"facet"_s, QString());

    QLineEdit* prefixEdit = settingsInput(u"prefix"_s, QString());
    prefixEdit->setMaximumWidth(80);

    QLineEdit* suffixEdit = settingsInput(u"suffix"_s, QString());
    suffixEdit->setMaximumWidth(80);

    if (!isAddRow) {
        const FacetFormat& format = m_settings->facetFormats[index];
        facetEdit->setText(format.facet);
        prefixEdit->setText(format.prefix);
        suffixEdit->setText(format.suffix);
    }

    auto* actionBtn = new QPushButton;
    actionBtn->setObjectName(u"ComposerRuleArgDelBtn"_s);
    actionBtn->setCursor(Qt::PointingHandCursor);
    actionBtn->setFocusPolicy(Qt::NoFocus);
    actionBtn->setFixedSize(20, 20);
    icons::applyStates(actionBtn, isAddRow ? icons::plus : icons::close, 10,
                       QColor(0x44, 0x44, 0x44), QColor(0xaa, 0x66, 0x66));

    if (isAddRow) {
        auto commitAdd = [this, facetEdit, prefixEdit, suffixEdit]() {
            const QString facet = facetEdit->text().trimmed();
            if (facet.isEmpty()) return;
            m_settings->facetFormats << FacetFormat{facet, prefixEdit->text(),
                                                    suffixEdit->text()};
            rebuildFacetFormats();
            emit settingsChanged();
        };
        connect(facetEdit, &QLineEdit::returnPressed, this, commitAdd);
        connect(prefixEdit, &QLineEdit::returnPressed, this, commitAdd);
        connect(suffixEdit, &QLineEdit::returnPressed, this, commitAdd);
        connect(actionBtn, &QPushButton::clicked, this, commitAdd);
    } else {
        // Clearing the facet name deletes the row.
        auto commitEdit = [this, index, facetEdit, prefixEdit, suffixEdit]() {
            if (index >= int(m_settings->facetFormats.size())) return;
            const QString facet = facetEdit->text().trimmed();
            if (facet.isEmpty()) {
                m_settings->facetFormats.removeAt(index);
                rebuildFacetFormats();
                emit settingsChanged();
                return;
            }
            m_settings->facetFormats[index].facet = facet;
            m_settings->facetFormats[index].prefix = prefixEdit->text();
            m_settings->facetFormats[index].suffix = suffixEdit->text();
            emit settingsChanged();
        };
        connect(facetEdit, &QLineEdit::editingFinished, this, commitEdit);
        connect(prefixEdit, &QLineEdit::editingFinished, this, commitEdit);
        connect(suffixEdit, &QLineEdit::editingFinished, this, commitEdit);
        connect(actionBtn, &QPushButton::clicked, this, [this, index]() {
            if (index >= int(m_settings->facetFormats.size())) return;
            m_settings->facetFormats.removeAt(index);
            rebuildFacetFormats();
            emit settingsChanged();
        });
    }

    layout->addWidget(facetEdit, 1);
    layout->addWidget(prefixEdit);
    layout->addWidget(suffixEdit);
    layout->addWidget(actionBtn);
    return row;
}

} // namespace tc
