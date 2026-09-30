#include <app/tag_editor_page.h>
#include <app/app_scroll_bar.h>
#include <app/icons.h>
#include <app/tag_search_bar.h>
#include <core/entry.h>
#include <core/settings.h>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

const QStringList kImageFilters = {u"*.png"_s, u"*.jpg"_s,  u"*.jpeg"_s,
                                   u"*.webp"_s, u"*.bmp"_s, u"*.gif"_s};

constexpr int kLeftWidth = 380;
constexpr int kRightWidth = 420;
constexpr int kRowHeight = 40;
constexpr int kHeaderHeight = 50;
constexpr int kSaveDebounceMs = 400;

// Background/foreground pairs, cycled so each token of the highlight pattern
// reads as its own colour against the dark editor.
const QList<QPair<QColor, QColor>> kHighlightColours = {
    {QColor(0x3a, 0x4a, 0x2a), QColor(0xe0, 0xff, 0xd0)}, // green
    {QColor(0x4a, 0x2a, 0x2a), QColor(0xff, 0xd0, 0xd0)}, // red
    {QColor(0x2a, 0x3a, 0x4a), QColor(0xd0, 0xe0, 0xff)}, // blue
    {QColor(0x3a, 0x2a, 0x4a), QColor(0xe0, 0xd0, 0xff)}, // purple
    {QColor(0x4a, 0x3a, 0x1a), QColor(0xff, 0xe0, 0xc0)}, // orange
};

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

QString sidecarFor(const QString& imagePath)
{
    const QFileInfo info(imagePath);
    return info.absolutePath() + u"/"_s + info.completeBaseName() + u".txt"_s;
}

} // namespace

// ---- TagSearchHighlighter

TagSearchHighlighter::TagSearchHighlighter(QTextDocument* parent) : QSyntaxHighlighter(parent) {}

void TagSearchHighlighter::setPattern(const QString& pattern)
{
    QStringList tokens;
    for (const QString& part : pattern.split(u',', Qt::SkipEmptyParts)) {
        const QString token = part.trimmed();
        if (!token.isEmpty()) tokens << token;
    }

    if (tokens == m_patterns) return;
    m_patterns = tokens;
    rehighlight();
}

void TagSearchHighlighter::highlightBlock(const QString& text)
{
    for (qsizetype i = 0; i < m_patterns.size(); ++i) {
        const QString& pattern = m_patterns[i];
        if (pattern.isEmpty()) continue;

        const auto& colours = kHighlightColours[i % kHighlightColours.size()];
        QTextCharFormat format;
        format.setBackground(colours.first);
        format.setForeground(colours.second);

        for (qsizetype from = 0;;) {
            const qsizetype at = text.indexOf(pattern, from, Qt::CaseInsensitive);
            if (at < 0) break;
            setFormat(int(at), int(pattern.size()), format);
            from = at + pattern.size();
        }
    }
}

// ---- TagEditorPage

TagEditorPage::TagEditorPage(Settings& settings, QWidget* parent)
    : QWidget(parent), m_settings(&settings)
{
    setObjectName(u"TagEditorPage"_s);
    setAttribute(Qt::WA_StyledBackground, true);

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(kSaveDebounceMs);
    connect(m_saveTimer, &QTimer::timeout, this, &TagEditorPage::saveNow);

    // ---- Left: folder and navigation
    auto* left = new QWidget;
    left->setObjectName(u"DatasetParamsPanel"_s);
    left->setAttribute(Qt::WA_StyledBackground, true);
    left->setFixedWidth(kLeftWidth);

    // Pairs with the WidgetWithChildrenShortcut arrows below: they step the
    // cursor, except inside a text field, which eats the key first.
    left->setFocusPolicy(Qt::ClickFocus);

    auto* leftBody = new QWidget;
    auto* leftLayout = new QVBoxLayout(leftBody);
    leftLayout->setContentsMargins(12, 12, 12, 12);
    leftLayout->setSpacing(8);

    m_folderEdit = new QLineEdit;
    m_folderEdit->setObjectName(u"SearchBar"_s);
    m_folderEdit->setPlaceholderText(u"Folder of images + .txt sidecars"_s);
    m_folderEdit->setFixedHeight(kRowHeight);

    auto* browse = new QPushButton(u"..."_s);
    browse->setObjectName(u"DatasetBrowseBtn"_s);
    browse->setFixedSize(36, kRowHeight);
    browse->setCursor(Qt::PointingHandCursor);

    auto* folderRow = new QHBoxLayout;
    folderRow->setContentsMargins(0, 0, 0, 0);
    folderRow->setSpacing(4);
    folderRow->addWidget(m_folderEdit, 1);
    folderRow->addWidget(browse);

    m_recursive = new QCheckBox(u"Recursive"_s);
    m_recursive->setObjectName(u"DatasetSoloCheck"_s);
    m_recursive->setChecked(true);
    m_recursive->setToolTip(u"Walk subdirectories of the chosen folder. Edits still write to\n"
                            u"the .txt beside each image, wherever it lives."_s);

    m_position = new QLabel;
    m_position->setObjectName(u"DatasetParamValue"_s);
    m_position->setAlignment(Qt::AlignCenter);

    auto navButton = [](const QString& text, const QString& tip) {
        auto* button = new QPushButton(text);
        button->setObjectName(u"DatasetBrowseBtn"_s);
        button->setFixedHeight(kRowHeight);
        button->setMinimumWidth(40);
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(tip);
        return button;
    };

    m_first = navButton(u"|<"_s, u"Jump to the first image"_s);
    m_back10 = navButton(u"<<"_s, u"Back 10 images"_s);
    m_back = navButton(u"<"_s, u"Back 1 image"_s);
    m_forward = navButton(u">"_s, u"Forward 1 image"_s);
    m_forward10 = navButton(u">>"_s, u"Forward 10 images"_s);
    m_last = navButton(u">|"_s, u"Jump to the last image"_s);

    auto* navRow = new QHBoxLayout;
    navRow->setContentsMargins(0, 0, 0, 0);
    navRow->setSpacing(4);
    for (QPushButton* button : {m_first, m_back10, m_back, m_forward, m_forward10, m_last})
        navRow->addWidget(button, 1);

    m_delete = new QPushButton(u"Delete (recycle bin)"_s);
    m_delete->setObjectName(u"EntryActionBtnDelete"_s);
    m_delete->setCursor(Qt::PointingHandCursor);
    m_delete->setToolTip(u"Move the current image and its .txt to the system recycle bin,\n"
                         u"where they stay until it is emptied."_s);

    m_sendToBatch = new QPushButton(u"Send to Batch Edit"_s);
    m_sendToBatch->setObjectName(u"EntryActionBtn"_s);
    m_sendToBatch->setCursor(Qt::PointingHandCursor);
    m_sendToBatch->setToolTip(u"Open this folder in Batch Edit for whole-folder operations."_s);

    // Label plus an "open in file manager" affordance, like the composer's
    // rules and vars headers.
    {
        auto* row = new QWidget;
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(4);

        auto* open = new QPushButton;
        open->setObjectName(u"SidebarBtn"_s);
        open->setFixedSize(20, 20);
        open->setIcon(icons::openExternal());
        open->setIconSize(QSize(14, 14));
        open->setCursor(Qt::PointingHandCursor);
        open->setToolTip(u"Open this folder in the system file manager."_s);
        connect(open, &QPushButton::clicked, this, [this]() {
            const QString folder = m_folderEdit->text().trimmed();
            if (folder.isEmpty() || !QDir(folder).exists()) return;
            QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
        });

        rowLayout->addWidget(paramLabel(u"Folder"_s), 1);
        rowLayout->addWidget(open);
        leftLayout->addWidget(row);
    }

    leftLayout->addLayout(folderRow);
    leftLayout->addWidget(m_recursive);
    leftLayout->addSpacing(8);
    leftLayout->addWidget(m_position);
    leftLayout->addLayout(navRow);
    leftLayout->addSpacing(8);
    leftLayout->addWidget(m_delete);
    leftLayout->addWidget(m_sendToBatch);
    leftLayout->addStretch();

    auto* leftColumn = new QVBoxLayout(left);
    leftColumn->setContentsMargins(0, 0, 0, 0);
    leftColumn->setSpacing(0);
    leftColumn->addWidget(sectionHeader(u"FOLDER"_s));
    leftColumn->addWidget(leftBody, 1);

    // ---- Middle: the image
    auto* middle = new QWidget;
    auto* middleBody = new QWidget;
    auto* middleBodyLayout = new QVBoxLayout(middleBody);
    middleBodyLayout->setContentsMargins(16, 16, 16, 16);
    middleBodyLayout->setSpacing(8);

    m_preview = new QLabel;
    m_preview->setObjectName(u"DatasetPreviewImage"_s);
    m_preview->setAttribute(Qt::WA_StyledBackground, true);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumHeight(360);
    m_preview->setCursor(Qt::PointingHandCursor);
    m_preview->setToolTip(u"Click to open the image in the system viewer."_s);
    m_preview->installEventFilter(this);

    // A label holding a pixmap reports the pixmap's size as its minimum, which
    // would pin the column to whatever image happens to be open. Expanding in
    // both directions, with no minimum width, lets the column drive instead.
    QSizePolicy policy = m_preview->sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::Expanding);
    policy.setVerticalPolicy(QSizePolicy::Expanding);
    policy.setHeightForWidth(false);
    m_preview->setSizePolicy(policy);
    m_preview->setMinimumWidth(0);

    middleBodyLayout->addWidget(m_preview, 1);

    // Built inline rather than with sectionHeader: this one carries the file
    // name beside the title.
    auto* imageHeader = new QWidget;
    imageHeader->setObjectName(u"DatasetSectionHeader"_s);
    imageHeader->setAttribute(Qt::WA_StyledBackground, true);
    imageHeader->setFixedHeight(kHeaderHeight);
    {
        auto* headerLayout = new QHBoxLayout(imageHeader);
        headerLayout->setContentsMargins(16, 12, 16, 12);
        headerLayout->setSpacing(12);

        auto* title = new QLabel(u"IMAGE"_s, imageHeader);
        title->setObjectName(u"DatasetSectionTitle"_s);

        m_imageName = new QLabel(imageHeader);
        m_imageName->setObjectName(u"DatasetSectionSubtitle"_s);
        m_imageName->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_imageName->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        headerLayout->addWidget(title);
        headerLayout->addWidget(m_imageName, 1);
    }

    auto* middleColumn = new QVBoxLayout(middle);
    middleColumn->setContentsMargins(0, 0, 0, 0);
    middleColumn->setSpacing(0);
    middleColumn->addWidget(imageHeader);
    middleColumn->addWidget(middleBody, 1);

    // ---- Right: the tags
    auto* right = new QWidget;
    right->setObjectName(u"DatasetPreviewPanel"_s);
    right->setAttribute(Qt::WA_StyledBackground, true);
    right->setFixedWidth(kRightWidth);

    auto* rightBody = new QWidget;
    auto* rightLayout = new QVBoxLayout(rightBody);
    rightLayout->setContentsMargins(12, 12, 12, 12);
    rightLayout->setSpacing(8);

    m_tagSearch = new TagSearchBar;
    m_tagSearch->setActiveTags(&m_activeTags);

    m_tagEdit = new QPlainTextEdit;
    m_tagEdit->setObjectName(u"DatasetExcludeEdit"_s);
    m_tagEdit->setPlaceholderText(u"Pick an image to edit its tags..."_s);
    m_tagEdit->setVerticalScrollBar(new AppScrollBar(Qt::Vertical));
    m_tagEdit->setEnabled(false);

    // Same object name as the editor so the two line up: same background,
    // border and font size.
    m_highlightEdit = new QLineEdit;
    m_highlightEdit->setObjectName(u"DatasetExcludeEdit"_s);
    m_highlightEdit->setPlaceholderText(u"Highlight (comma-separated)..."_s);
    m_highlightEdit->setClearButtonEnabled(true);
    m_highlightEdit->setFixedHeight(kRowHeight);
    m_highlightEdit->setToolTip(u"Comma-separated substrings. Each gets its own colour in the\n"
                                u"editor, so separate matches stay apart. Case-insensitive."_s);

    m_status = new QLabel;
    m_status->setObjectName(u"DatasetStatusLabel"_s);

    rightLayout->addWidget(paramLabel(u"Add tag"_s));
    rightLayout->addWidget(m_tagSearch);
    rightLayout->addWidget(m_tagEdit, 1);
    rightLayout->addWidget(paramLabel(u"Highlight"_s));
    rightLayout->addWidget(m_highlightEdit);
    rightLayout->addWidget(m_status);

    auto* rightColumn = new QVBoxLayout(right);
    rightColumn->setContentsMargins(0, 0, 0, 0);
    rightColumn->setSpacing(0);
    rightColumn->addWidget(sectionHeader(u"EDITOR"_s));
    rightColumn->addWidget(rightBody, 1);

    m_highlighter = new TagSearchHighlighter(m_tagEdit->document());

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(left);
    root->addWidget(middle, 1);
    root->addWidget(right);

    if (!m_settings->tagEditorFolder.isEmpty()) {
        m_folderEdit->setText(m_settings->tagEditorFolder);
        m_pendingScan = true;
    }

    // ---- Wiring
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString folder = QFileDialog::getExistingDirectory(this, u"Choose folder"_s,
                                                                 m_folderEdit->text());
        if (folder.isEmpty()) return;
        m_folderEdit->setText(folder);
        persistSettings();
        rescan();
    });
    connect(m_folderEdit, &QLineEdit::editingFinished, this, [this]() {
        persistSettings();
        rescan();
    });
    connect(m_recursive, &QCheckBox::toggled, this, [this](bool) { rescan(); });

    connect(m_highlightEdit, &QLineEdit::textChanged, this,
            [this](const QString& text) { m_highlighter->setPattern(text); });

    // Scoped to the left panel so the arrows keep working in the text fields.
    for (auto [key, step] : {std::pair{Qt::Key_Left, -1}, std::pair{Qt::Key_Right, 1}}) {
        auto* shortcut = new QShortcut(QKeySequence(key), left);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this,
                [this, step]() { jumpTo(m_index + step); });
    }

    connect(m_first, &QPushButton::clicked, this, [this]() { jumpTo(0); });
    connect(m_back10, &QPushButton::clicked, this, [this]() { jumpTo(m_index - 10); });
    connect(m_back, &QPushButton::clicked, this, [this]() { jumpTo(m_index - 1); });
    connect(m_forward, &QPushButton::clicked, this, [this]() { jumpTo(m_index + 1); });
    connect(m_forward10, &QPushButton::clicked, this, [this]() { jumpTo(m_index + 10); });
    connect(m_last, &QPushButton::clicked, this,
            [this]() { jumpTo(int(m_images.size()) - 1); });

    connect(m_delete, &QPushButton::clicked, this, &TagEditorPage::deleteCurrent);

    connect(m_sendToBatch, &QPushButton::clicked, this, [this]() {
        const QString folder = m_folderEdit->text().trimmed();
        if (folder.isEmpty()) return;
        emit sendToBatchEditRequested(folder);
    });

    // Nothing to hand over without a folder, so the button says so.
    auto syncSend = [this]() {
        m_sendToBatch->setEnabled(!m_folderEdit->text().trimmed().isEmpty());
    };
    syncSend();
    connect(m_folderEdit, &QLineEdit::textChanged, this, [syncSend](const QString&) { syncSend(); });

    connect(m_tagEdit, &QPlainTextEdit::textChanged, this, [this]() {
        rebuildActiveTags();
        scheduleSave();
    });
    connect(m_tagSearch, &TagSearchBar::tagAdded, this, &TagEditorPage::appendTag);

    updateNavButtons();
}

TagEditorPage::~TagEditorPage()
{
    // A pending edit is still only in the widget; tearing down without this
    // loses whatever was typed in the last fraction of a second.
    if (m_saveTimer->isActive()) {
        m_saveTimer->stop();
        saveNow();
    }
}

void TagEditorPage::setInputFolder(const QString& folder)
{
    m_folderEdit->setText(folder);
    persistSettings();
    m_pendingScan = false; // scanning now, so the first show must not repeat it
    rescan();
}

void TagEditorPage::setDanbooruIndex(const DanbooruIndex* index)
{
    m_danbooru = index;
    m_tagSearch->setIndex(index);
}

void TagEditorPage::rescan()
{
    // Flush before the current path is forgotten.
    if (m_saveTimer->isActive()) {
        m_saveTimer->stop();
        saveNow();
    }

    m_images.clear();
    m_index = -1;
    m_imagePath.clear();
    m_txtPath.clear();
    m_previewSource = {};
    m_preview->clear();
    m_imageName->clear();
    m_status->clear();

    {
        QSignalBlocker blocker(m_tagEdit);
        m_tagEdit->clear();
        m_tagEdit->setEnabled(false);
    }

    const QString folder = m_folderEdit->text().trimmed();
    if (folder.isEmpty() || !QDir(folder).exists()) {
        updateNavButtons();
        return;
    }

    const QDirIterator::IteratorFlags flags = m_recursive->isChecked()
                                                  ? QDirIterator::Subdirectories
                                                  : QDirIterator::NoIteratorFlags;
    QDirIterator it(folder, kImageFilters, QDir::Files, flags);
    while (it.hasNext()) m_images << it.next();
    std::sort(m_images.begin(), m_images.end());

    if (m_images.isEmpty()) {
        m_status->setText(u"No images in folder."_s);
        updateNavButtons();
        return;
    }

    jumpTo(0);
}

void TagEditorPage::jumpTo(int index)
{
    if (m_images.isEmpty()) {
        m_index = -1;
        m_imagePath.clear();
        m_txtPath.clear();
        m_previewSource = {};
        m_preview->clear();
        m_imageName->clear();
        m_status->setText(u"No images in folder."_s);

        QSignalBlocker blocker(m_tagEdit);
        m_tagEdit->clear();
        m_tagEdit->setEnabled(false);
        updateNavButtons();
        return;
    }

    // The outgoing image's edits belong to the outgoing image.
    if (m_saveTimer->isActive()) {
        m_saveTimer->stop();
        saveNow();
    }

    // Clamped, so callers can say `m_index - 10` without checking bounds.
    index = std::clamp(index, 0, int(m_images.size()) - 1);

    // Deleted from under us: drop it and take the next one along.
    if (!QFileInfo::exists(m_images.at(index))) {
        m_images.removeAt(index);
        jumpTo(std::min(index, int(m_images.size()) - 1));
        return;
    }

    m_index = index;
    m_imagePath = m_images.at(index);
    m_txtPath = sidecarFor(m_imagePath);

    QImageReader reader(m_imagePath);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        m_previewSource = {};
        m_preview->clear();
        m_preview->setText(u"(image failed to decode)"_s);
    }
    else {
        m_previewSource = QPixmap::fromImage(image);
        rescalePreview();
    }

    QString contents;
    if (QFile file(m_txtPath); file.open(QIODevice::ReadOnly | QIODevice::Text))
        contents = QString::fromUtf8(file.readAll());

    {
        QSignalBlocker blocker(m_tagEdit);
        m_tagEdit->setEnabled(true);
        m_tagEdit->setPlainText(contents);
    }

    // Relative to the chosen folder, so a recursive scan shows subdir/foo.png
    // rather than an absolute path that does not fit.
    m_imageName->setText(QDir(m_folderEdit->text().trimmed()).relativeFilePath(m_imagePath));

    const QString name = QFileInfo(m_txtPath).fileName();
    m_status->setText(QFile::exists(m_txtPath) ? u"Editing %1"_s.arg(name)
                                               : u"New: %1"_s.arg(name));

    rebuildActiveTags();
    updateNavButtons();
}

void TagEditorPage::updateNavButtons()
{
    const int total = int(m_images.size());
    const bool any = total > 0;

    m_position->setText(u"%1 / %2"_s.arg(any ? m_index + 1 : 0).arg(total));

    const bool canBack = any && m_index > 0;
    const bool canForward = any && m_index < total - 1;

    for (QPushButton* button : {m_first, m_back10, m_back}) button->setEnabled(canBack);
    for (QPushButton* button : {m_forward, m_forward10, m_last}) button->setEnabled(canForward);
    m_delete->setEnabled(any);
}

void TagEditorPage::deleteCurrent()
{
    if (m_index < 0 || m_index >= m_images.size()) return;

    const QString imagePath = m_imagePath;
    const QString txtPath = m_txtPath;

    // Drop the pending save first, or it writes the sidecar back out right
    // after the delete.
    m_saveTimer->stop();
    m_imagePath.clear();
    m_txtPath.clear();

    bool ok = true;
    if (QFile::exists(imagePath) && !QFile::moveToTrash(imagePath)) ok = false;
    if (QFile::exists(txtPath) && !QFile::moveToTrash(txtPath)) ok = false;

    if (!ok) {
        // Put the paths back so a retry does not need the place found again.
        m_imagePath = imagePath;
        m_txtPath = txtPath;
        m_status->setText(u"Delete failed (recycle bin unavailable)."_s);
        return;
    }

    // Holding the index lands on the next image, which is what deleting a run
    // of bad images wants; the last one steps back instead.
    m_images.removeAt(m_index);
    if (m_index >= m_images.size()) --m_index;

    jumpTo(m_index);
    if (!m_images.isEmpty())
        m_status->setText(u"Moved %1 to recycle bin."_s.arg(QFileInfo(imagePath).fileName()));
}

void TagEditorPage::appendTag(const QString& tag)
{
    if (!m_tagEdit->isEnabled()) return;

    const QString text = m_tagEdit->toPlainText().trimmed();
    m_tagEdit->setPlainText(text.isEmpty() ? tag : text + u", "_s + tag);
}

void TagEditorPage::rebuildActiveTags()
{
    m_activeTags.clear();
    for (const QString& part : m_tagEdit->toPlainText().split(u',', Qt::SkipEmptyParts)) {
        const QString tag = normalizeTag(part);
        if (!tag.isEmpty()) m_activeTags.insert(tag);
    }
}

void TagEditorPage::scheduleSave()
{
    if (m_txtPath.isEmpty()) return;
    m_saveTimer->start();
}

void TagEditorPage::saveNow()
{
    if (m_txtPath.isEmpty()) return;

    QFile file(m_txtPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        m_status->setText(u"Save failed: %1"_s.arg(file.errorString()));
        return;
    }
    file.write(m_tagEdit->toPlainText().toUtf8());
    m_status->setText(u"Saved %1"_s.arg(QFileInfo(m_txtPath).fileName()));
}

void TagEditorPage::persistSettings()
{
    m_settings->tagEditorFolder = m_folderEdit->text().trimmed();
}

void TagEditorPage::rescalePreview()
{
    if (m_previewSource.isNull()) return;

    const QSize area = m_preview->size();
    if (area.width() <= 0 || area.height() <= 0) return;

    m_preview->setPixmap(roundedScaled(m_previewSource, area.width(), area.height(), 8.0));
}

void TagEditorPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    if (!m_pendingScan) return;
    m_pendingScan = false;
    if (!m_folderEdit->text().trimmed().isEmpty()) rescan();
}

bool TagEditorPage::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_preview) return QWidget::eventFilter(watched, event);

    // The label's own resize is what "the available area changed" means here.
    // Not consumed: the label still has its own work to do.
    if (event->type() == QEvent::Resize) rescalePreview();

    if (event->type() == QEvent::MouseButtonRelease) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton && !m_imagePath.isEmpty()
            && QFile::exists(m_imagePath)) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_imagePath));
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace tc
