#include <gui/dataset/autotagpage.h>
#include <gui/widgets/appscrollbar.h>
#include <gui/widgets/composericons.h>
#include <core/autotaggerlibrary.h>
#include <core/batchtagger.h>
#include <core/soundplayer.h>
#include <utils/appsettings.h>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QSlider>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QListWidget>
#include <QListWidgetItem>
#include <QDesktopServices>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QShowEvent>
#include <QUrl>

namespace gui {

namespace {
constexpr int kPanelWidth = 380;
constexpr int kPreviewPanelWidth = 420;
// Folder row line-edit and "..." button share this height for alignment;
// the #SearchBar QSS at font-size 16 plus 6+6 padding needs about 40 px,
// and anything shorter clips descenders.
constexpr int kFolderRowHeight = 40;

// Slider stores integer hundredths of the threshold (0.00 to 1.00).
constexpr int kThresholdMin = 0;
constexpr int kThresholdMax = 100;

QPixmap roundedScaled(const QPixmap& src, int maxW, int maxH, qreal radius)
{
    if (src.isNull()) return {};
    const QPixmap scaled = src.scaled(maxW, maxH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPixmap rounded(scaled.size());
    rounded.fill(Qt::transparent);
    QPainter p(&rounded);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    QPainterPath path;
    path.addRoundedRect(QRectF(rounded.rect()), radius, radius);
    p.setClipPath(path);
    p.drawPixmap(0, 0, scaled);
    return rounded;
}

// Mirrors the section header used across the dataset helpers page so all
// three sub-pages share the same top strip.
QWidget* makeSectionHeader(QWidget* parent, const QString& title)
{
    auto* header = new QWidget(parent);
    header->setObjectName("DatasetSectionHeader");
    header->setAttribute(Qt::WA_StyledBackground, true);
    header->setFixedHeight(50);
    auto* l = new QHBoxLayout(header);
    l->setContentsMargins(16, 12, 16, 12);
    l->setSpacing(8);
    auto* lbl = new QLabel(title, header);
    lbl->setObjectName("DatasetSectionTitle");
    l->addWidget(lbl);
    l->addStretch();
    return header;
}
} // namespace

// ---- Construction

AutoTagPage::AutoTagPage(core::AutoTaggerLibrary* library, utils::AppSettings* settings,
                         QWidget* parent)
    : QWidget(parent), m_library(library), m_settings(settings)
{
    setObjectName("AutoTagPage");
    setAttribute(Qt::WA_StyledBackground, true);

    m_runner = new core::BatchTagger(this);

    // ---- Left: params
    auto* paramsPanel = new QWidget(this);
    paramsPanel->setObjectName("DatasetParamsPanel");
    paramsPanel->setAttribute(Qt::WA_StyledBackground, true);
    paramsPanel->setFixedWidth(kPanelWidth);

    auto* pl = new QVBoxLayout(paramsPanel);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->setSpacing(0);

    auto* paramsBody = new QWidget(paramsPanel);
    auto* pbl = new QVBoxLayout(paramsBody);
    pbl->setContentsMargins(12, 12, 12, 12);
    pbl->setSpacing(8);

    auto mkLabel = [&](const QString& t) -> QLabel* {
        auto* l = new QLabel(t, paramsBody);
        l->setObjectName("DatasetParamLabel");
        return l;
    };

    // Mirrors the composer's RULES/VARS header chip: label + right-
    // aligned "open in file manager" icon.
    auto mkFolderLabel = [&](const QString& t, QLineEdit* edit) -> QWidget* {
        auto* row = new QWidget(paramsBody);
        auto* l = new QHBoxLayout(row);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(4);

        auto* lbl = new QLabel(t, row);
        lbl->setObjectName("DatasetParamLabel");

        auto* openBtn = new QPushButton(row);
        openBtn->setObjectName("SidebarBtn");
        openBtn->setFixedSize(20, 20);
        openBtn->setIcon(gui::icons::openExternal());
        openBtn->setIconSize(QSize(14, 14));
        openBtn->setCursor(Qt::PointingHandCursor);
        openBtn->setToolTip(QString("Open this folder in the system file manager."));
        connect(openBtn, &QPushButton::clicked, this, [edit]() {
            const QString d = edit->text().trimmed();
            if (d.isEmpty() || !QDir(d).exists()) return;
            QDesktopServices::openUrl(QUrl::fromLocalFile(d));
        });

        l->addWidget(lbl, 1);
        l->addWidget(openBtn);
        return row;
    };

    m_modelBox = new QComboBox(paramsBody);
    m_modelBox->setObjectName("DatasetSpin");
    m_modelBox->setToolTip("Active autotagger model. Drop a folder under data/models/ with\n"
                           "model.onnx + config.json + tags.csv to add another.");
    refreshModels();

    m_inputEdit = new QLineEdit(paramsBody);
    m_inputEdit->setObjectName("SearchBar");
    m_inputEdit->setPlaceholderText("Folder containing images");
    m_inputEdit->setToolTip("Folder of images to tag. Recursive by default.");
    m_inputEdit->setFixedHeight(kFolderRowHeight);
    m_inputBrowseBtn = new QPushButton("…", paramsBody);
    m_inputBrowseBtn->setObjectName("DatasetBrowseBtn");
    m_inputBrowseBtn->setFixedSize(36, kFolderRowHeight);

    auto* inputRow = new QHBoxLayout;
    inputRow->setContentsMargins(0, 0, 0, 0);
    inputRow->setSpacing(4);
    inputRow->addWidget(m_inputEdit, 1);
    inputRow->addWidget(m_inputBrowseBtn);

    m_outputEdit = new QLineEdit(paramsBody);
    m_outputEdit->setObjectName("SearchBar");
    m_outputEdit->setPlaceholderText("Where the .txt sidecars go");
    m_outputEdit->setToolTip("Output folder for the .txt sidecars. The input subtree is mirrored\n"
                             "under this folder. Existing .txt files are overwritten.");
    m_outputEdit->setFixedHeight(kFolderRowHeight);
    m_outBrowseBtn = new QPushButton("…", paramsBody);
    m_outBrowseBtn->setObjectName("DatasetBrowseBtn");
    m_outBrowseBtn->setFixedSize(36, kFolderRowHeight);

    auto* outputRow = new QHBoxLayout;
    outputRow->setContentsMargins(0, 0, 0, 0);
    outputRow->setSpacing(4);
    outputRow->addWidget(m_outputEdit, 1);
    outputRow->addWidget(m_outBrowseBtn);

    m_thresholdSlider = new QSlider(Qt::Horizontal, paramsBody);
    m_thresholdSlider->setObjectName("DatasetPmiSlider");
    m_thresholdSlider->setRange(kThresholdMin, kThresholdMax);
    m_thresholdSlider->setValue(35); // 0.35
    m_thresholdSlider->setToolTip(
        "Confidence cutoff for tags written to .txt. Tags with scores below\n"
        "this are dropped.");

    m_thresholdLbl = new QLabel(paramsBody);
    m_thresholdLbl->setObjectName("DatasetParamValue");
    m_thresholdLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_thresholdLbl->setMinimumWidth(36);

    auto syncThresholdLbl = [this]() {
        m_thresholdLbl->setText(QString::number(m_thresholdSlider->value() / 100.0, 'f', 2));
    };

    auto* thresholdRow = new QHBoxLayout;
    thresholdRow->setContentsMargins(0, 0, 0, 0);
    thresholdRow->setSpacing(6);
    thresholdRow->addWidget(m_thresholdSlider, 1);
    thresholdRow->addWidget(m_thresholdLbl);

    m_cooldownSpin = new QSpinBox(paramsBody);
    m_cooldownSpin->setObjectName("DatasetSpin");
    m_cooldownSpin->setRange(0, 5000);
    m_cooldownSpin->setSingleStep(50);
    m_cooldownSpin->setSuffix(" ms");
    m_cooldownSpin->setToolTip("Pause inserted between each image's inference. Higher values give\n"
                               "the CPU room to cool down between hits - useful on laptops or\n"
                               "during long batches. 0 = run as fast as possible.");

    m_recursiveCheck = new QCheckBox("Recursive", paramsBody);
    m_recursiveCheck->setObjectName("DatasetSoloCheck");
    m_recursiveCheck->setChecked(true);
    m_recursiveCheck->setToolTip("Walk subdirectories of the input folder. The same subtree is\n"
                                 "mirrored under the output folder.");

    m_moveCheck = new QCheckBox("Move images to output", paramsBody);
    m_moveCheck->setObjectName("DatasetSoloCheck");
    m_moveCheck->setToolTip("After tagging, move each source image into the output folder\n"
                            "next to its .txt sidecar. Has no effect when input and output\n"
                            "are the same folder.");

    m_runBtn = new QPushButton("Run", paramsBody);
    m_cancelBtn = new QPushButton("Cancel", paramsBody);
    m_runBtn->setObjectName("DatasetRunBtn");
    m_cancelBtn->setObjectName("EntryActionBtn");
    m_cancelBtn->setEnabled(false);

    m_sendToEditorBtn = new QPushButton("Send to Tag Editor", paramsBody);
    m_sendToEditorBtn->setObjectName("EntryActionBtn");
    m_sendToEditorBtn->setToolTip(
        "Open the output folder in the Tag Editor tab for review and editing.");

    m_sendToBatchBtn = new QPushButton("Send to Batch Edit", paramsBody);
    m_sendToBatchBtn->setObjectName("EntryActionBtn");
    m_sendToBatchBtn->setToolTip(
        "Open the output folder in the Batch Edit tab for whole-folder ops.");

    pbl->addWidget(mkLabel("Model"));
    pbl->addWidget(m_modelBox);
    pbl->addWidget(mkFolderLabel("Input folder", m_inputEdit));
    pbl->addLayout(inputRow);
    pbl->addWidget(mkFolderLabel("Output folder", m_outputEdit));
    pbl->addLayout(outputRow);
    pbl->addWidget(mkLabel("Threshold"));
    pbl->addLayout(thresholdRow);
    pbl->addWidget(mkLabel("Cooldown"));
    pbl->addWidget(m_cooldownSpin);
    pbl->addWidget(m_recursiveCheck);
    pbl->addWidget(m_moveCheck);
    pbl->addSpacing(8);
    pbl->addWidget(m_runBtn);
    pbl->addWidget(m_cancelBtn);
    pbl->addSpacing(8);
    pbl->addWidget(m_sendToEditorBtn);
    pbl->addWidget(m_sendToBatchBtn);
    pbl->addStretch();

    pl->addWidget(makeSectionHeader(paramsPanel, "AUTO-TAGGER"));
    pl->addWidget(paramsBody, 1);

    // ---- Middle: results
    auto* resultsPanel = new QWidget(this);
    auto* rl = new QVBoxLayout(resultsPanel);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);

    auto* resultsBody = new QWidget(resultsPanel);
    auto* rbl = new QVBoxLayout(resultsBody);
    rbl->setContentsMargins(12, 12, 12, 12);
    rbl->setSpacing(8);

    // Status label: empty initially - the centered empty-state owns the
    // pre-run prompt copy. Status fills in once a run is in progress.
    m_statusLabel = new QLabel(resultsBody);
    m_statusLabel->setObjectName("DatasetStatusLabel");

    m_progressBar = new QProgressBar(resultsBody);
    m_progressBar->setObjectName("DatasetProgressBar");
    m_progressBar->setTextVisible(true);
    m_progressBar->setVisible(false);

    m_resultsList = new QListWidget(resultsBody);
    m_resultsList->setObjectName("DatasetResultsList");
    m_resultsList->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));
    m_resultsList->setFrameShape(QFrame::NoFrame);

    // Empty state stacked with m_resultsList; same pattern as TagClusterPage.
    m_emptyState = new QWidget(resultsBody);
    m_emptyState->setObjectName("DatasetEmptyContainer");
    auto* ecl = new QVBoxLayout(m_emptyState);
    ecl->setContentsMargins(0, 0, 0, 0);
    ecl->setSpacing(0);
    m_emptyStateLbl = new QLabel("Pick an input folder and click Run.", m_emptyState);
    m_emptyStateLbl->setObjectName("DatasetEmptyState");
    m_emptyStateLbl->setAlignment(Qt::AlignCenter);
    ecl->addStretch();
    ecl->addWidget(m_emptyStateLbl, 0, Qt::AlignHCenter);
    ecl->addStretch();

    rbl->addWidget(m_statusLabel);
    rbl->addWidget(m_progressBar);
    rbl->addWidget(m_resultsList, 1);
    rbl->addWidget(m_emptyState, 1);

    // Initial state: empty placeholder visible, results list hidden.
    m_resultsList->setVisible(false);

    rl->addWidget(makeSectionHeader(resultsPanel, "RESULTS"));
    rl->addWidget(resultsBody, 1);

    // ---- Right: preview
    auto* previewPanel = new QWidget(this);
    previewPanel->setObjectName("DatasetPreviewPanel");
    previewPanel->setAttribute(Qt::WA_StyledBackground, true);
    previewPanel->setFixedWidth(kPreviewPanelWidth);

    auto* prl = new QVBoxLayout(previewPanel);
    prl->setContentsMargins(0, 0, 0, 0);
    prl->setSpacing(0);

    auto* previewBody = new QWidget(previewPanel);
    auto* pvl = new QVBoxLayout(previewBody);
    pvl->setContentsMargins(12, 16, 12, 12);
    pvl->setSpacing(8);

    m_focusImage = new QLabel(previewBody);
    m_focusImage->setObjectName("DatasetPreviewImage");
    m_focusImage->setAttribute(Qt::WA_StyledBackground, true);
    m_focusImage->setAlignment(Qt::AlignCenter);
    m_focusImage->setMinimumHeight(220);
    m_focusImage->setCursor(Qt::PointingHandCursor);
    m_focusImage->installEventFilter(this);
    m_focusImage->setToolTip("Click to open the image in the system viewer.");

    m_focusRating = new QLabel(previewBody);
    m_focusRating->setObjectName("DatasetStatusLabel");
    m_focusRating->setAlignment(Qt::AlignCenter);

    m_focusTags = new QListWidget(previewBody);
    m_focusTags->setObjectName("DatasetResultsList");
    m_focusTags->setFrameShape(QFrame::NoFrame);
    m_focusTags->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));

    pvl->addWidget(m_focusImage, 0, Qt::AlignHCenter);
    pvl->addWidget(m_focusRating);
    pvl->addWidget(m_focusTags, 1);

    prl->addWidget(makeSectionHeader(previewPanel, "PREVIEW"));
    prl->addWidget(previewBody, 1);

    // ---- Root
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(paramsPanel);
    root->addWidget(resultsPanel, 1);
    root->addWidget(previewPanel);

    // ---- Hydrate from settings
    if (m_settings) {
        if (!m_settings->autoTagInputFolder.isEmpty())
            m_inputEdit->setText(m_settings->autoTagInputFolder);
        if (!m_settings->autoTagOutputFolder.isEmpty())
            m_outputEdit->setText(m_settings->autoTagOutputFolder);
        m_thresholdSlider->setValue(int(m_settings->autoTagThreshold * 100.0f));
        m_cooldownSpin->setValue(qBound(0, m_settings->autoTagCooldownMs, 5000));

        const int idx = m_modelBox->findText(m_settings->activeAutoTagModel);
        if (idx >= 0) m_modelBox->setCurrentIndex(idx);
    }
    syncThresholdLbl();

    // ---- Wire
    connect(m_inputBrowseBtn, &QPushButton::clicked, this, [this]() {
        const QString d =
            QFileDialog::getExistingDirectory(this, "Choose input folder", m_inputEdit->text());
        if (!d.isEmpty()) {
            m_inputEdit->setText(d);
            persistSettings();
        }
    });
    connect(m_outBrowseBtn, &QPushButton::clicked, this, [this]() {
        const QString d =
            QFileDialog::getExistingDirectory(this, "Choose output folder", m_outputEdit->text());
        if (!d.isEmpty()) {
            m_outputEdit->setText(d);
            persistSettings();
        }
    });
    connect(m_inputEdit, &QLineEdit::editingFinished, this, [this]() { persistSettings(); });
    connect(m_outputEdit, &QLineEdit::editingFinished, this, [this]() { persistSettings(); });

    connect(m_modelBox, &QComboBox::currentTextChanged, this,
            [this](const QString&) { persistSettings(); });

    connect(m_thresholdSlider, &QSlider::valueChanged, this, [this, syncThresholdLbl](int) {
        syncThresholdLbl();
        persistSettings();
    });
    connect(m_cooldownSpin, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int) { persistSettings(); });

    connect(m_runBtn, &QPushButton::clicked, this, &AutoTagPage::onRun);
    connect(m_cancelBtn, &QPushButton::clicked, this, &AutoTagPage::onCancel);

    connect(m_sendToEditorBtn, &QPushButton::clicked, this, [this]() {
        const QString out = m_outputEdit->text().trimmed();
        if (out.isEmpty()) return;
        emit editFolderRequested(out);
    });
    connect(m_sendToBatchBtn, &QPushButton::clicked, this, [this]() {
        const QString out = m_outputEdit->text().trimmed();
        if (out.isEmpty()) return;
        emit sendToBatchEditRequested(out);
    });

    // The two "Send to..." buttons need the output folder; sync the
    // enabled state on every textChanged so picking via browse or typing
    // enables them as soon as the field is non-empty.
    auto syncSendButtons = [this]() {
        const bool ok = !m_outputEdit->text().trimmed().isEmpty();
        m_sendToEditorBtn->setEnabled(ok);
        m_sendToBatchBtn->setEnabled(ok);
    };
    syncSendButtons();
    connect(m_outputEdit, &QLineEdit::textChanged, this,
            [syncSendButtons](const QString&) { syncSendButtons(); });

    connect(m_resultsList, &QListWidget::currentItemChanged, this,
            &AutoTagPage::onResultRowChanged);

    connect(m_runner, &core::BatchTagger::scanned, this, &AutoTagPage::onScanned);
    connect(m_runner, &core::BatchTagger::imageTagged, this, &AutoTagPage::onImageTagged);
    connect(m_runner, &core::BatchTagger::imageFailed, this, &AutoTagPage::onImageFailed);
    connect(m_runner, &core::BatchTagger::progress, this, &AutoTagPage::onProgress);
    connect(m_runner, &core::BatchTagger::finished, this, &AutoTagPage::onFinished);
}

AutoTagPage::~AutoTagPage()
{
    if (m_runner) m_runner->cancel();
}

void AutoTagPage::setInputFolder(const QString& folder)
{
    if (m_inputEdit) m_inputEdit->setText(folder);
    persistSettings();
}

void AutoTagPage::refreshModels()
{
    if (!m_library) return;
    const QString prev = m_modelBox ? m_modelBox->currentText() : QString();
    if (m_modelBox) {
        m_modelBox->clear();
        m_modelBox->addItems(m_library->availableModels());
        const int idx = m_modelBox->findText(prev);
        if (idx >= 0) m_modelBox->setCurrentIndex(idx);
    }
}

// ---- Run / cancel

void AutoTagPage::showEmptyState(const QString& message)
{
    const bool empty = !message.isEmpty();
    if (empty && m_emptyStateLbl) m_emptyStateLbl->setText(message);
    if (m_emptyState) m_emptyState->setVisible(empty);
    if (m_resultsList) m_resultsList->setVisible(!empty);
}

void AutoTagPage::onRun()
{
    if (!m_library) {
        m_statusLabel->setText("No model library available.");
        return;
    }

    // Pick up model folders added/removed since launch; refreshModels
    // keeps the dropdown on whatever is still present.
    m_library->rescan();
    refreshModels();

    if (m_library->availableModels().isEmpty()) {
        m_statusLabel->setText("No models found under data/models/. Each model needs its own "
                               "subfolder containing model.onnx, config.json, and tags.csv.");
        return;
    }

    const QString modelName = m_modelBox->currentText();
    if (modelName.isEmpty()) {
        m_statusLabel->setText("Pick a model from the dropdown first.");
        return;
    }

    auto* model = m_library->model(modelName);
    if (!model) {
        m_statusLabel->setText(
            QString("Model '%1' failed to load - check that its model.onnx, config.json, "
                    "and tags.csv are all present and valid.")
                .arg(modelName));
        return;
    }

    const QString inFolder = m_inputEdit->text().trimmed();
    const QString outFolder = m_outputEdit->text().trimmed();
    if (inFolder.isEmpty() || outFolder.isEmpty()) {
        m_statusLabel->setText("Set both input and output folders first.");
        return;
    }
    if (!QDir(inFolder).exists()) {
        m_statusLabel->setText(QString("Input folder doesn't exist: %1").arg(inFolder));
        return;
    }

    m_results.clear();
    m_failures.clear();
    m_resultsList->clear();
    m_focusedRel.clear();
    m_focusImage->clear();
    m_focusImage->setText(QString());
    m_focusRating->clear();
    m_focusTags->clear();

    setRunning(true);
    m_statusLabel->setText("Scanning…");
    showEmptyState("Scanning input folder…");
    persistSettings();

    m_runner->start(model, inFolder, outFolder, m_thresholdSlider->value() / 100.0f,
                    m_recursiveCheck->isChecked(), m_moveCheck->isChecked(),
                    m_cooldownSpin->value());
}

void AutoTagPage::onCancel()
{
    if (!m_runner) return;
    m_runner->cancel();
    m_statusLabel->setText("Cancelling…");
    m_cancelBtn->setEnabled(false);
}

// ---- Worker callbacks

void AutoTagPage::onScanned(int total)
{
    m_progressBar->setRange(0, total);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(true);
    m_progressBar->setFormat(QString("%v / %m"));
    m_statusLabel->setText(QString("Tagging %1 images…").arg(total));
}

void AutoTagPage::onImageTagged(QString relPath, core::TagResult result)
{
    m_results.insert(relPath, result);

    // First result of the run flips the list visible - empty state shrinks
    // and the user starts seeing rows immediately.
    if (m_resultsList->count() == 0 && m_emptyState->isVisible()) showEmptyState({});

    auto* item = new QListWidgetItem(QString("%1   (%2 tags)").arg(relPath).arg(result.tags.size()),
                                     m_resultsList);
    item->setData(Qt::UserRole, relPath);

    // Follow latest: setCurrentItem fires currentItemChanged into
    // showFocusedResult so the preview tracks each new image.
    m_resultsList->setCurrentItem(item);
    m_resultsList->scrollToItem(item);
}

void AutoTagPage::onImageFailed(QString relPath, QString reason)
{
    m_failures.insert(relPath, reason);
    auto* item = new QListWidgetItem(QString("✗  %1   (%2)").arg(relPath, reason), m_resultsList);
    item->setData(Qt::UserRole, relPath);
}

void AutoTagPage::onProgress(int done, int total)
{
    m_progressBar->setRange(0, total);
    m_progressBar->setValue(done);
}

void AutoTagPage::onFinished(bool cancelled)
{
    setRunning(false);
    if (cancelled)
        m_statusLabel->setText(QString("Cancelled - %1 done.").arg(m_results.size()));
    else {
        m_statusLabel->setText(QString("Done - %1 images tagged, %2 failed.")
                                   .arg(m_results.size())
                                   .arg(m_failures.size()));
        if (auto* sp = core::SoundPlayer::instance()) sp->play("finish");
    }

    // Restore the placeholder if no rows landed (zero images or all failed).
    if (m_resultsList->count() == 0) {
        showEmptyState(cancelled ? "Cancelled before any images were tagged."
                                 : "No images found in the input folder.");
    }
}

// ---- Result row to preview

void AutoTagPage::onResultRowChanged(QListWidgetItem* current, QListWidgetItem*)
{
    if (!current) return;
    m_focusedRel = current->data(Qt::UserRole).toString();
    showFocusedResult();
}

void AutoTagPage::showFocusedResult()
{
    m_focusTags->clear();
    m_focusRating->clear();

    if (m_focusedRel.isEmpty()) {
        m_focusImage->clear();
        return;
    }

    // Decode the source image at preview size.
    const QString fullPath = m_inputEdit->text().trimmed() + "/" + m_focusedRel;
    QImageReader reader(fullPath);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (!img.isNull()) {
        m_focusImage->setPixmap(roundedScaled(QPixmap::fromImage(img), 296, 320, 6.0));
    }
    else {
        m_focusImage->clear();
    }

    if (auto it = m_results.constFind(m_focusedRel); it != m_results.constEnd()) {
        const core::TagResult& r = it.value();
        if (!r.rating.isEmpty()) {
            m_focusRating->setText(QString("Rating: %1  (%2)")
                                       .arg(r.rating)
                                       .arg(QString::number(r.ratingScore, 'f', 2)));
        }
        for (const core::TagPrediction& tp : r.tags) {
            new QListWidgetItem(QString("%1   %2").arg(tp.tag, QString::number(tp.score, 'f', 2)),
                                m_focusTags);
        }
        // Dimmed near-misses (top-N below the threshold) so the user can
        // see what lowering the slider would bring in without re-running.
        for (const core::TagPrediction& tp : r.nearMisses) {
            auto* item = new QListWidgetItem(
                QString("%1   %2").arg(tp.tag, QString::number(tp.score, 'f', 2)), m_focusTags);
            item->setForeground(QColor("#555555"));
            item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        }
    }
    else if (auto fit = m_failures.constFind(m_focusedRel); fit != m_failures.constEnd()) {
        m_focusRating->setText("Failed: " + fit.value());
    }
}

// ---- UI state

void AutoTagPage::setRunning(bool on)
{
    m_runBtn->setEnabled(!on);
    m_cancelBtn->setEnabled(on);
    m_modelBox->setEnabled(!on);
    m_inputEdit->setEnabled(!on);
    m_inputBrowseBtn->setEnabled(!on);
    m_outputEdit->setEnabled(!on);
    m_outBrowseBtn->setEnabled(!on);
    m_thresholdSlider->setEnabled(!on);
    m_cooldownSpin->setEnabled(!on);
    m_recursiveCheck->setEnabled(!on);
    m_moveCheck->setEnabled(!on);
    if (!on) m_progressBar->setVisible(false);
}

bool AutoTagPage::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == m_focusImage && ev->type() == QEvent::MouseButtonRelease) {
        auto* me = static_cast<QMouseEvent*>(ev);
        if (me->button() == Qt::LeftButton && !m_focusedRel.isEmpty()) {
            const QString full = m_inputEdit->text().trimmed() + "/" + m_focusedRel;
            if (QFile::exists(full)) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(full));
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, ev);
}

void AutoTagPage::showEvent(QShowEvent* ev)
{
    QWidget::showEvent(ev);
    // Pick up external model-folder changes; the active name survives if
    // it's still present.
    if (m_library) {
        m_library->rescan();
        refreshModels();
    }
}

void AutoTagPage::persistSettings()
{
    if (!m_settings) return;
    m_settings->activeAutoTagModel = m_modelBox->currentText();
    m_settings->autoTagThreshold = m_thresholdSlider->value() / 100.0f;
    m_settings->autoTagCooldownMs = m_cooldownSpin->value();
    m_settings->autoTagInputFolder = m_inputEdit->text().trimmed();
    m_settings->autoTagOutputFolder = m_outputEdit->text().trimmed();
}

} // namespace gui
