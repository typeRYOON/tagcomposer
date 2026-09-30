#include <app/auto_tag_page.h>
#include <app/app_scroll_bar.h>
#include <app/icons.h>
#include <core/settings.h>
#include <tagger/batch_tagger.h>
#include <tagger/tagger_library.h>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QShowEvent>
#include <QSlider>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

constexpr int kParamsWidth = 380;
constexpr int kPreviewWidth = 420;
constexpr int kHeaderHeight = 50;

// Tall enough for #SearchBar's padding; the browse button matches.
constexpr int kRowHeight = 40;

// Hundredths of the 0-1 threshold.
constexpr int kThresholdMin = 0;
constexpr int kThresholdMax = 100;

constexpr int kCooldownMax = 5000;
constexpr int kPreviewImageWidth = 296;
constexpr int kPreviewImageHeight = 320;

QPixmap roundedScaled(const QPixmap& source, int maxWidth, int maxHeight, qreal radius)
{
    if (source.isNull()) return {};

    const QPixmap scaled =
        source.scaled(maxWidth, maxHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPixmap rounded(scaled.size());
    rounded.fill(Qt::transparent);

    QPainter painter(&rounded);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    QPainterPath path;
    path.addRoundedRect(QRectF(rounded.rect()), radius, radius);
    painter.setClipPath(path);
    painter.drawPixmap(0, 0, scaled);
    return rounded;
}

QWidget* sectionHeader(const QString& title)
{
    auto* header = new QWidget;
    header->setObjectName(u"DatasetSectionHeader"_s);
    header->setAttribute(Qt::WA_StyledBackground, true);
    header->setFixedHeight(kHeaderHeight);

    auto* layout = new QHBoxLayout(header);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(8);

    auto* label = new QLabel(title, header);
    label->setObjectName(u"DatasetSectionTitle"_s);
    layout->addWidget(label);
    layout->addStretch();
    return header;
}

QLabel* paramLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(u"DatasetParamLabel"_s);
    return label;
}

} // namespace

AutoTagPage::AutoTagPage(TaggerLibrary& library, Settings& settings, QWidget* parent)
    : QWidget(parent), m_library(&library), m_settings(&settings)
{
    setObjectName(u"AutoTagPage"_s);
    setAttribute(Qt::WA_StyledBackground, true);

    m_runner = new BatchTagger(this);

    // ---- Left: what to run, and over what
    auto* params = new QWidget;
    params->setObjectName(u"DatasetParamsPanel"_s);
    params->setAttribute(Qt::WA_StyledBackground, true);
    params->setFixedWidth(kParamsWidth);

    auto* paramsBody = new QWidget;
    auto* paramsLayout = new QVBoxLayout(paramsBody);
    paramsLayout->setContentsMargins(12, 12, 12, 12);
    paramsLayout->setSpacing(8);

    // Label plus an "open in file manager" button.
    auto folderLabel = [this](const QString& text, QLineEdit* edit) {
        auto* row = new QWidget;
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);

        auto* open = new QPushButton;
        open->setObjectName(u"SidebarBtn"_s);
        open->setFixedSize(20, 20);
        open->setIcon(icons::openExternal());
        open->setIconSize(QSize(14, 14));
        open->setCursor(Qt::PointingHandCursor);
        open->setToolTip(u"Open this folder in the system file manager."_s);
        connect(open, &QPushButton::clicked, this, [edit]() {
            const QString folder = edit->text().trimmed();
            if (folder.isEmpty() || !QDir(folder).exists()) return;
            QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
        });

        layout->addWidget(paramLabel(text), 1);
        layout->addWidget(open);
        return row;
    };

    m_models = new QComboBox;
    m_models->setObjectName(u"DatasetSpin"_s);
    m_models->setToolTip(u"Each model is a folder under the data dir's models/ holding\n"
                         u"model.onnx, config.json and tags.csv."_s);

    m_inputEdit = new QLineEdit;
    m_inputEdit->setObjectName(u"SearchBar"_s);
    m_inputEdit->setPlaceholderText(u"Folder of images to tag"_s);
    m_inputEdit->setFixedHeight(kRowHeight);
    m_inputEdit->setToolTip(u"The images to run over."_s);

    m_inputBrowse = new QPushButton(u"..."_s);
    m_inputBrowse->setObjectName(u"DatasetBrowseBtn"_s);
    m_inputBrowse->setFixedSize(36, kRowHeight);
    m_inputBrowse->setCursor(Qt::PointingHandCursor);

    auto* inputRow = new QHBoxLayout;
    inputRow->setContentsMargins(0, 0, 0, 0);
    inputRow->setSpacing(4);
    inputRow->addWidget(m_inputEdit, 1);
    inputRow->addWidget(m_inputBrowse);

    m_outputEdit = new QLineEdit;
    m_outputEdit->setObjectName(u"SearchBar"_s);
    m_outputEdit->setPlaceholderText(u"Folder for the .txt sidecars"_s);
    m_outputEdit->setFixedHeight(kRowHeight);
    m_outputEdit->setToolTip(u"Where the .txt files go. A recursive run mirrors the input's\n"
                             u"subdirectories here rather than flattening them."_s);

    m_outputBrowse = new QPushButton(u"..."_s);
    m_outputBrowse->setObjectName(u"DatasetBrowseBtn"_s);
    m_outputBrowse->setFixedSize(36, kRowHeight);
    m_outputBrowse->setCursor(Qt::PointingHandCursor);

    auto* outputRow = new QHBoxLayout;
    outputRow->setContentsMargins(0, 0, 0, 0);
    outputRow->setSpacing(4);
    outputRow->addWidget(m_outputEdit, 1);
    outputRow->addWidget(m_outputBrowse);

    m_threshold = new QSlider(Qt::Horizontal);
    m_threshold->setObjectName(u"DatasetPmiSlider"_s);
    m_threshold->setRange(kThresholdMin, kThresholdMax);
    m_threshold->setValue(35);
    m_threshold->setToolTip(u"Confidence a tag needs to be written out. Lower takes more and\n"
                            u"is noisier. What falls just below still shows in the preview,\n"
                            u"dimmed, so the effect of moving this is visible without a\n"
                            u"second run."_s);

    m_thresholdValue = new QLabel;
    m_thresholdValue->setObjectName(u"DatasetParamValue"_s);
    m_thresholdValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_thresholdValue->setMinimumWidth(40);

    auto syncThreshold = [this]() {
        m_thresholdValue->setText(QString::number(m_threshold->value() / 100.0, 'f', 2));
    };
    syncThreshold();

    auto* thresholdRow = new QHBoxLayout;
    thresholdRow->setContentsMargins(0, 0, 0, 0);
    thresholdRow->setSpacing(6);
    thresholdRow->addWidget(m_threshold, 1);
    thresholdRow->addWidget(m_thresholdValue);

    m_cooldown = new QSpinBox;
    m_cooldown->setObjectName(u"DatasetSpin"_s);
    m_cooldown->setRange(0, kCooldownMax);
    m_cooldown->setValue(100);
    m_cooldown->setSuffix(u" ms"_s);
    m_cooldown->setToolTip(u"A pause between images. Raise it to leave the machine usable\n"
                           u"during a long batch."_s);

    m_recursive = new QCheckBox(u"Recursive"_s);
    m_recursive->setObjectName(u"DatasetSoloCheck"_s);
    m_recursive->setChecked(true);
    m_recursive->setToolTip(u"Walk subdirectories, and mirror them under the output folder."_s);

    m_moveImages = new QCheckBox(u"Move images to output"_s);
    m_moveImages->setObjectName(u"DatasetSoloCheck"_s);
    m_moveImages->setToolTip(u"Relocate each image next to its .txt once tagged. Does nothing\n"
                             u"when the two folders are the same."_s);

    m_runBtn = new QPushButton(u"Run"_s);
    m_runBtn->setObjectName(u"DatasetRunBtn"_s);
    m_runBtn->setCursor(Qt::PointingHandCursor);

    m_cancelBtn = new QPushButton(u"Cancel"_s);
    m_cancelBtn->setObjectName(u"DatasetCancelBtn"_s);
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setEnabled(false);
    m_cancelBtn->setToolTip(u"Stop after the image being worked on. Everything already\n"
                            u"written stays."_s);

    m_sendToEditor = new QPushButton(u"Send to Tag Editor"_s);
    m_sendToEditor->setObjectName(u"EntryActionBtn"_s);
    m_sendToEditor->setCursor(Qt::PointingHandCursor);
    m_sendToEditor->setToolTip(u"Open the output folder in the Tag Editor."_s);

    m_sendToBatch = new QPushButton(u"Send to Batch Edit"_s);
    m_sendToBatch->setObjectName(u"EntryActionBtn"_s);
    m_sendToBatch->setCursor(Qt::PointingHandCursor);
    m_sendToBatch->setToolTip(u"Open the output folder in Batch Edit."_s);

    paramsLayout->addWidget(paramLabel(u"Model"_s));
    paramsLayout->addWidget(m_models);
    paramsLayout->addWidget(folderLabel(u"Input folder"_s, m_inputEdit));
    paramsLayout->addLayout(inputRow);
    paramsLayout->addWidget(folderLabel(u"Output folder"_s, m_outputEdit));
    paramsLayout->addLayout(outputRow);
    paramsLayout->addWidget(paramLabel(u"Threshold"_s));
    paramsLayout->addLayout(thresholdRow);
    paramsLayout->addWidget(paramLabel(u"Cooldown"_s));
    paramsLayout->addWidget(m_cooldown);
    paramsLayout->addWidget(m_recursive);
    paramsLayout->addWidget(m_moveImages);
    paramsLayout->addSpacing(8);
    paramsLayout->addWidget(m_runBtn);
    paramsLayout->addWidget(m_cancelBtn);
    paramsLayout->addSpacing(8);
    paramsLayout->addWidget(m_sendToEditor);
    paramsLayout->addWidget(m_sendToBatch);
    paramsLayout->addStretch();

    auto* paramsColumn = new QVBoxLayout(params);
    paramsColumn->setContentsMargins(0, 0, 0, 0);
    paramsColumn->setSpacing(0);
    paramsColumn->addWidget(sectionHeader(u"AUTO-TAGGER"_s));
    paramsColumn->addWidget(paramsBody, 1);

    // ---- Middle: what came back
    auto* results = new QWidget;
    auto* resultsBody = new QWidget;
    auto* resultsLayout = new QVBoxLayout(resultsBody);
    resultsLayout->setContentsMargins(12, 12, 12, 12);
    resultsLayout->setSpacing(8);

    m_status = new QLabel;
    m_status->setObjectName(u"DatasetStatusLabel"_s);

    m_progress = new QProgressBar;
    m_progress->setObjectName(u"DatasetProgressBar"_s);
    m_progress->setTextVisible(true);
    m_progress->setVisible(false);

    m_results = new QListWidget;
    m_results->setObjectName(u"DatasetResultsList"_s);
    m_results->setVerticalScrollBar(new AppScrollBar(Qt::Vertical));
    m_results->setFrameShape(QFrame::NoFrame);
    m_results->setVisible(false);

    m_emptyState = new QWidget;
    m_emptyState->setObjectName(u"DatasetEmptyContainer"_s);
    auto* emptyLayout = new QVBoxLayout(m_emptyState);
    emptyLayout->setContentsMargins(0, 0, 0, 0);
    emptyLayout->setSpacing(0);

    m_emptyLabel = new QLabel(u"Pick an input folder and click Run."_s, m_emptyState);
    m_emptyLabel->setObjectName(u"DatasetEmptyState"_s);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLayout->addStretch();
    emptyLayout->addWidget(m_emptyLabel, 0, Qt::AlignHCenter);
    emptyLayout->addStretch();

    resultsLayout->addWidget(m_status);
    resultsLayout->addWidget(m_progress);
    resultsLayout->addWidget(m_results, 1);
    resultsLayout->addWidget(m_emptyState, 1);

    auto* resultsColumn = new QVBoxLayout(results);
    resultsColumn->setContentsMargins(0, 0, 0, 0);
    resultsColumn->setSpacing(0);
    resultsColumn->addWidget(sectionHeader(u"RESULTS"_s));
    resultsColumn->addWidget(resultsBody, 1);

    // ---- Right: one image and its tags
    auto* previewPanel = new QWidget;
    previewPanel->setObjectName(u"DatasetPreviewPanel"_s);
    previewPanel->setAttribute(Qt::WA_StyledBackground, true);
    previewPanel->setFixedWidth(kPreviewWidth);

    auto* previewBody = new QWidget;
    auto* previewLayout = new QVBoxLayout(previewBody);
    previewLayout->setContentsMargins(12, 16, 12, 12);
    previewLayout->setSpacing(8);

    m_focusImage = new QLabel;
    m_focusImage->setObjectName(u"DatasetPreviewImage"_s);
    m_focusImage->setAttribute(Qt::WA_StyledBackground, true);
    m_focusImage->setAlignment(Qt::AlignCenter);
    m_focusImage->setMinimumHeight(220);
    m_focusImage->setCursor(Qt::PointingHandCursor);
    m_focusImage->setToolTip(u"Click to open the image in the system viewer."_s);
    m_focusImage->installEventFilter(this);

    m_focusRating = new QLabel;
    m_focusRating->setObjectName(u"DatasetStatusLabel"_s);
    m_focusRating->setAlignment(Qt::AlignCenter);

    m_focusTags = new QListWidget;
    m_focusTags->setObjectName(u"DatasetResultsList"_s);
    m_focusTags->setFrameShape(QFrame::NoFrame);
    m_focusTags->setVerticalScrollBar(new AppScrollBar(Qt::Vertical));

    previewLayout->addWidget(m_focusImage, 0, Qt::AlignHCenter);
    previewLayout->addWidget(m_focusRating);
    previewLayout->addWidget(m_focusTags, 1);

    auto* previewColumn = new QVBoxLayout(previewPanel);
    previewColumn->setContentsMargins(0, 0, 0, 0);
    previewColumn->setSpacing(0);
    previewColumn->addWidget(sectionHeader(u"PREVIEW"_s));
    previewColumn->addWidget(previewBody, 1);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(params);
    root->addWidget(results, 1);
    root->addWidget(previewPanel);

    // ---- Hydrate
    refreshModels();
    if (!m_settings->autoTagInputFolder.isEmpty())
        m_inputEdit->setText(m_settings->autoTagInputFolder);
    if (!m_settings->autoTagOutputFolder.isEmpty())
        m_outputEdit->setText(m_settings->autoTagOutputFolder);
    m_threshold->setValue(int(m_settings->autoTagThreshold * 100.0f));
    m_cooldown->setValue(std::clamp(m_settings->autoTagCooldownMs, 0, kCooldownMax));
    if (const int at = m_models->findText(m_settings->autoTagModel); at >= 0)
        m_models->setCurrentIndex(at);
    syncThreshold();

    // ---- Wiring
    auto browseInto = [this](QLineEdit* edit, const QString& title) {
        const QString folder = QFileDialog::getExistingDirectory(this, title, edit->text());
        if (folder.isEmpty()) return;
        edit->setText(folder);
        persistSettings();
    };
    connect(m_inputBrowse, &QPushButton::clicked, this,
            [this, browseInto]() { browseInto(m_inputEdit, u"Choose input folder"_s); });
    connect(m_outputBrowse, &QPushButton::clicked, this,
            [this, browseInto]() { browseInto(m_outputEdit, u"Choose output folder"_s); });

    connect(m_inputEdit, &QLineEdit::editingFinished, this, &AutoTagPage::persistSettings);
    connect(m_outputEdit, &QLineEdit::editingFinished, this, &AutoTagPage::persistSettings);
    connect(m_models, &QComboBox::currentTextChanged, this,
            [this](const QString&) { persistSettings(); });
    connect(m_threshold, &QSlider::valueChanged, this, [this, syncThreshold](int) {
        syncThreshold();
        persistSettings();
    });
    connect(m_cooldown, &QSpinBox::valueChanged, this, [this](int) { persistSettings(); });

    connect(m_runBtn, &QPushButton::clicked, this, &AutoTagPage::run);
    connect(m_cancelBtn, &QPushButton::clicked, this, &AutoTagPage::cancel);

    connect(m_sendToEditor, &QPushButton::clicked, this, [this]() {
        const QString folder = m_outputEdit->text().trimmed();
        if (!folder.isEmpty()) emit editFolderRequested(folder);
    });
    connect(m_sendToBatch, &QPushButton::clicked, this, [this]() {
        const QString folder = m_outputEdit->text().trimmed();
        if (!folder.isEmpty()) emit sendToBatchEditRequested(folder);
    });

    // Handoffs need an output folder.
    auto syncSendButtons = [this]() {
        const bool ready = !m_outputEdit->text().trimmed().isEmpty();
        m_sendToEditor->setEnabled(ready);
        m_sendToBatch->setEnabled(ready);
    };
    syncSendButtons();
    connect(m_outputEdit, &QLineEdit::textChanged, this,
            [syncSendButtons](const QString&) { syncSendButtons(); });

    connect(m_results, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current, QListWidgetItem*) {
                if (!current) return;
                m_focused = current->data(Qt::UserRole).toString();
                showFocused();
            });

    connect(m_runner, &BatchTagger::scanned, this, &AutoTagPage::onScanned);
    connect(m_runner, &BatchTagger::imageTagged, this, &AutoTagPage::onImageTagged);
    connect(m_runner, &BatchTagger::imageFailed, this, &AutoTagPage::onImageFailed);
    connect(m_runner, &BatchTagger::progress, this, [this](int done, int total) {
        m_progress->setRange(0, total);
        m_progress->setValue(done);
    });
    connect(m_runner, &BatchTagger::finished, this, &AutoTagPage::onFinished);
}

AutoTagPage::~AutoTagPage()
{
    m_runner->cancel();
}

void AutoTagPage::setInputFolder(const QString& folder)
{
    m_inputEdit->setText(folder);
    persistSettings();
}

void AutoTagPage::refreshModels()
{
    const QString previous = m_models->currentText();

    m_models->clear();
    m_models->addItems(m_library->availableModels());

    // Keep the selection across rescans.
    if (const int at = m_models->findText(previous); at >= 0) m_models->setCurrentIndex(at);
}

void AutoTagPage::showEmptyState(const QString& message)
{
    const bool empty = !message.isEmpty();
    if (empty) m_emptyLabel->setText(message);
    m_emptyState->setVisible(empty);
    m_results->setVisible(!empty);
}

void AutoTagPage::run()
{
    // Pick up model folders added since the last scan.
    m_library->rescan();
    refreshModels();

    if (m_library->availableModels().isEmpty()) {
        m_status->setText(u"No models found. Each one needs its own folder under the data "
                          u"dir's models/, holding model.onnx, config.json and tags.csv."_s);
        return;
    }

    const QString modelName = m_models->currentText();
    if (modelName.isEmpty()) {
        m_status->setText(u"Pick a model first."_s);
        return;
    }

    TaggerModel* model = m_library->model(modelName);
    if (!model) {
        m_status->setText(u"Model '%1' failed to load. Check that model.onnx, config.json and "
                          u"tags.csv are all present and readable."_s.arg(modelName));
        return;
    }

    const QString input = m_inputEdit->text().trimmed();
    const QString output = m_outputEdit->text().trimmed();
    if (input.isEmpty() || output.isEmpty()) {
        m_status->setText(u"Set both the input and output folders first."_s);
        return;
    }
    if (!QDir(input).exists()) {
        m_status->setText(u"Input folder does not exist: %1"_s.arg(input));
        return;
    }

    m_tagged.clear();
    m_failed.clear();
    m_results->clear();
    m_focused.clear();
    m_focusImage->clear();
    m_focusRating->clear();
    m_focusTags->clear();

    setRunning(true);
    m_status->setText(u"Scanning..."_s);
    showEmptyState(u"Scanning input folder..."_s);
    persistSettings();

    m_runner->start(model, input, output, m_threshold->value() / 100.0f, m_recursive->isChecked(),
                    m_moveImages->isChecked(), m_cooldown->value());
}

void AutoTagPage::cancel()
{
    m_runner->cancel();
    m_status->setText(u"Cancelling..."_s);
    m_cancelBtn->setEnabled(false);
}

void AutoTagPage::onScanned(int total)
{
    m_progress->setRange(0, total);
    m_progress->setValue(0);
    m_progress->setFormat(u"%v / %m"_s);
    m_progress->setVisible(true);
    m_status->setText(u"Tagging %1 images..."_s.arg(total));
}

void AutoTagPage::onImageTagged(const QString& relativePath, const TaggerResult& result)
{
    m_tagged.insert(relativePath, result);

    // Swap in the list on the first result.
    if (m_results->count() == 0 && m_emptyState->isVisible()) showEmptyState(QString());

    auto* item = new QListWidgetItem(
        u"%1   (%2 tags)"_s.arg(relativePath).arg(result.tags.size()), m_results);
    item->setData(Qt::UserRole, relativePath);

    // Follow the newest row so the preview tracks the run.
    m_results->setCurrentItem(item);
    m_results->scrollToItem(item);
}

void AutoTagPage::onImageFailed(const QString& relativePath, const QString& reason)
{
    m_failed.insert(relativePath, reason);

    auto* item = new QListWidgetItem(u"%1   (%2)"_s.arg(relativePath, reason), m_results);
    item->setIcon(icons::close(11, QColor(0xaa, 0x44, 0x44)));
    item->setData(Qt::UserRole, relativePath);
}

void AutoTagPage::onFinished(bool cancelled)
{
    setRunning(false);

    m_status->setText(
        cancelled ? u"Cancelled - %1 done."_s.arg(m_tagged.size())
                  : u"Done - %1 tagged, %2 failed."_s.arg(m_tagged.size()).arg(m_failed.size()));

    // Nothing landed; show why.
    if (m_results->count() > 0) return;
    showEmptyState(cancelled ? u"Cancelled before any image was tagged."_s
                             : u"No images found in the input folder."_s);
}

void AutoTagPage::showFocused()
{
    m_focusTags->clear();
    m_focusRating->clear();

    if (m_focused.isEmpty()) {
        m_focusImage->clear();
        return;
    }

    QImageReader reader(m_inputEdit->text().trimmed() + u"/"_s + m_focused);
    reader.setAutoTransform(true);

    const QImage image = reader.read();
    if (image.isNull())
        m_focusImage->clear();
    else
        m_focusImage->setPixmap(roundedScaled(QPixmap::fromImage(image), kPreviewImageWidth,
                                              kPreviewImageHeight, 6.0));

    if (const auto failure = m_failed.constFind(m_focused); failure != m_failed.cend()) {
        m_focusRating->setText(u"Failed: "_s + failure.value());
        return;
    }

    const auto found = m_tagged.constFind(m_focused);
    if (found == m_tagged.cend()) return;

    const TaggerResult& result = found.value();
    if (!result.rating.isEmpty())
        m_focusRating->setText(
            u"Rating: %1  (%2)"_s.arg(result.rating, QString::number(result.ratingScore, 'f', 2)));

    for (const TaggerPrediction& prediction : result.tags)
        new QListWidgetItem(
            u"%1   %2"_s.arg(prediction.tag, QString::number(prediction.score, 'f', 2)),
            m_focusTags);

    // Near misses, dimmed and unselectable.
    for (const TaggerPrediction& prediction : result.nearMisses) {
        auto* item = new QListWidgetItem(
            u"%1   %2"_s.arg(prediction.tag, QString::number(prediction.score, 'f', 2)),
            m_focusTags);
        item->setForeground(QColor(0x55, 0x55, 0x55));
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    }
}

void AutoTagPage::setRunning(bool running)
{
    m_runBtn->setEnabled(!running);
    m_cancelBtn->setEnabled(running);

    // Lock the run's inputs while it runs.
    m_models->setEnabled(!running);
    m_inputEdit->setEnabled(!running);
    m_inputBrowse->setEnabled(!running);
    m_outputEdit->setEnabled(!running);
    m_outputBrowse->setEnabled(!running);
    m_threshold->setEnabled(!running);
    m_cooldown->setEnabled(!running);
    m_recursive->setEnabled(!running);
    m_moveImages->setEnabled(!running);

    if (!running) m_progress->setVisible(false);
}

bool AutoTagPage::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_focusImage || event->type() != QEvent::MouseButtonRelease)
        return QWidget::eventFilter(watched, event);

    auto* mouse = static_cast<QMouseEvent*>(event);
    if (mouse->button() == Qt::LeftButton && !m_focused.isEmpty()) {
        const QString path = m_inputEdit->text().trimmed() + u"/"_s + m_focused;
        if (QFile::exists(path)) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void AutoTagPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    // Pick up model folders changed outside the app.
    m_library->rescan();
    refreshModels();
}

void AutoTagPage::persistSettings()
{
    m_settings->autoTagModel = m_models->currentText();
    m_settings->autoTagThreshold = m_threshold->value() / 100.0f;
    m_settings->autoTagCooldownMs = m_cooldown->value();
    m_settings->autoTagInputFolder = m_inputEdit->text().trimmed();
    m_settings->autoTagOutputFolder = m_outputEdit->text().trimmed();
}

} // namespace tc
