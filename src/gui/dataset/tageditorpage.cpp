#include <gui/dataset/tageditorpage.h>
#include <gui/widgets/appscrollbar.h>
#include <gui/widgets/composericons.h>
#include <gui/widgets/tagsearchbar.h>
#include <utils/appsettings.h>
#include <utils/stringutils.h>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPlainTextEdit>
#include <QLabel>
#include <QTimer>
#include <QFileDialog>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QKeySequence>
#include <QMenu>
#include <QMouseEvent>
#include <QShortcut>
#include <QPainter>
#include <QPainterPath>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QUrl>

namespace gui {

namespace {
const QStringList kImageFilters = {"*.png", "*.jpg", "*.jpeg", "*.webp", "*.bmp", "*.gif"};

constexpr int kLeftPanelWidth = 380;  // matches AutoTagPage's left
constexpr int kRightPanelWidth = 420; // matches AutoTagPage's right
constexpr int kFolderRowHeight = 40;
constexpr int kPreviewMaxWidth = 720; // upper bound for the centered image
constexpr int kPreviewMaxHeight = 720;

// Background/foreground pairs the highlighter cycles through so each
// comma-separated token gets a distinct colour. Tuned for the dark theme.
const QList<QPair<QColor, QColor>> kHighlightColors = {
    {QColor("#3a4a2a"), QColor("#e0ffd0")}, // green
    {QColor("#4a2a2a"), QColor("#ffd0d0")}, // red
    {QColor("#2a3a4a"), QColor("#d0e0ff")}, // blue
    {QColor("#3a2a4a"), QColor("#e0d0ff")}, // purple
    {QColor("#4a3a1a"), QColor("#ffe0c0")}, // orange
};

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

QString sidecarPathForImage(const QString& imagePath)
{
    const QFileInfo fi(imagePath);
    return fi.absolutePath() + "/" + fi.completeBaseName() + ".txt";
}
} // namespace

// ── TagSearchHighlighter ─────────────────────────────────────────────────────

TagSearchHighlighter::TagSearchHighlighter(QTextDocument* parent) : QSyntaxHighlighter(parent) {}

void TagSearchHighlighter::setPattern(const QString& pattern)
{
    QStringList tokens;
    for (const QString& part : pattern.split(',', Qt::SkipEmptyParts)) {
        const QString t = part.trimmed();
        if (!t.isEmpty()) tokens << t;
    }
    if (tokens == m_patterns) return;
    m_patterns = tokens;
    rehighlight();
}

void TagSearchHighlighter::highlightBlock(const QString& text)
{
    if (m_patterns.isEmpty()) return;

    for (int i = 0; i < m_patterns.size(); ++i) {
        const QString& pat = m_patterns[i];
        if (pat.isEmpty()) continue;

        const auto colors = kHighlightColors[i % kHighlightColors.size()];
        QTextCharFormat fmt;
        fmt.setBackground(colors.first);
        fmt.setForeground(colors.second);

        int from = 0;
        while (true) {
            const int idx = text.indexOf(pat, from, Qt::CaseInsensitive);
            if (idx < 0) break;
            setFormat(idx, pat.length(), fmt);
            from = idx + pat.length();
        }
    }
}

// ── TagEditorPage ────────────────────────────────────────────────────────────

TagEditorPage::TagEditorPage(core::DanbooruIndex* danbooruIndex, utils::AppSettings* settings,
                             QWidget* parent)
    : QWidget(parent), m_danbooruIndex(danbooruIndex), m_settings(settings)
{
    setObjectName("TagEditorPage");
    setAttribute(Qt::WA_StyledBackground, true);

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(400);
    connect(m_saveTimer, &QTimer::timeout, this, &TagEditorPage::saveNow);

    // ── Left column ─────────────────────────────────────────────────────────
    auto* leftPanel = new QWidget(this);
    leftPanel->setObjectName("DatasetParamsPanel");
    leftPanel->setAttribute(Qt::WA_StyledBackground, true);
    leftPanel->setFixedWidth(kLeftPanelWidth);
    // Click-to-focus pairs with the WidgetWithChildrenShortcut bindings
    // below: arrows step the navigation cursor, except inside text fields
    // where the line-edit consumes the key first.
    leftPanel->setFocusPolicy(Qt::ClickFocus);

    auto* ll = new QVBoxLayout(leftPanel);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->setSpacing(0);

    auto* leftBody = new QWidget(leftPanel);
    auto* lbl = new QVBoxLayout(leftBody);
    lbl->setContentsMargins(12, 12, 12, 12);
    lbl->setSpacing(8);

    auto mkLabel = [&](const QString& t) -> QLabel* {
        auto* l = new QLabel(t, leftBody);
        l->setObjectName("DatasetParamLabel");
        return l;
    };

    m_folderEdit = new QLineEdit(leftBody);
    m_folderEdit->setObjectName("SearchBar");
    m_folderEdit->setPlaceholderText("Folder of images + .txt sidecars");
    m_folderEdit->setFixedHeight(kFolderRowHeight);
    m_browseBtn = new QPushButton("…", leftBody);
    m_browseBtn->setObjectName("DatasetBrowseBtn");
    m_browseBtn->setFixedSize(36, kFolderRowHeight);

    auto* folderRow = new QHBoxLayout;
    folderRow->setContentsMargins(0, 0, 0, 0);
    folderRow->setSpacing(4);
    folderRow->addWidget(m_folderEdit, 1);
    folderRow->addWidget(m_browseBtn);

    m_recursiveCheck = new QCheckBox("Recursive", leftBody);
    m_recursiveCheck->setObjectName("DatasetSoloCheck");
    m_recursiveCheck->setChecked(true);
    m_recursiveCheck->setToolTip("Walk subdirectories of the chosen folder. The image list shows\n"
                                 "each match with its path relative to the root, and edits write\n"
                                 "back to the matching .txt next to each image.");

    // Position counter: "12 / 47" centered above the nav buttons.
    m_positionLbl = new QLabel(leftBody);
    m_positionLbl->setObjectName("DatasetParamValue");
    m_positionLbl->setAlignment(Qt::AlignCenter);

    auto mkNavBtn = [&](const QString& glyph, const QString& tip) {
        auto* b = new QPushButton(glyph, leftBody);
        b->setObjectName("DatasetBrowseBtn");
        b->setFixedHeight(kFolderRowHeight);
        b->setMinimumWidth(40);
        b->setToolTip(tip);
        return b;
    };

    m_navFirstBtn = mkNavBtn(QStringLiteral("|<"), "Jump to the first image");
    m_navPrev10Btn = mkNavBtn(QStringLiteral("<<"), "Back 10 images");
    m_navPrevBtn = mkNavBtn(QStringLiteral("<"), "Back 1 image");
    m_navNextBtn = mkNavBtn(QStringLiteral(">"), "Forward 1 image");
    m_navNext10Btn = mkNavBtn(QStringLiteral(">>"), "Forward 10 images");
    m_navLastBtn = mkNavBtn(QStringLiteral(">|"), "Jump to the last image");

    auto* navRow = new QHBoxLayout;
    navRow->setContentsMargins(0, 0, 0, 0);
    navRow->setSpacing(4);
    navRow->addWidget(m_navFirstBtn, 1);
    navRow->addWidget(m_navPrev10Btn, 1);
    navRow->addWidget(m_navPrevBtn, 1);
    navRow->addWidget(m_navNextBtn, 1);
    navRow->addWidget(m_navNext10Btn, 1);
    navRow->addWidget(m_navLastBtn, 1);

    m_deleteBtn = new QPushButton("Delete (recycle bin)", leftBody);
    m_deleteBtn->setObjectName("EntryActionBtnDelete");
    m_deleteBtn->setToolTip("Move the current image and its .txt to the system recycle bin.\n"
                            "Recoverable from the OS Recycle Bin until the user empties it.");

    m_sendToBatchBtn = new QPushButton("Send to Batch Edit", leftBody);
    m_sendToBatchBtn->setObjectName("EntryActionBtn");
    m_sendToBatchBtn->setToolTip("Open this folder in the Batch Edit tab for whole-folder ops.");

    // Mirrors the composer's RULES/VARS header chip: label + "open in
    // file manager" affordance.
    {
        auto* row = new QWidget(leftBody);
        auto* l = new QHBoxLayout(row);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(4);
        auto* lblw = new QLabel("Folder", row);
        lblw->setObjectName("DatasetParamLabel");
        auto* openBtn = new QPushButton(row);
        openBtn->setObjectName("SidebarBtn");
        openBtn->setFixedSize(20, 20);
        openBtn->setIcon(gui::icons::openExternal());
        openBtn->setIconSize(QSize(14, 14));
        openBtn->setCursor(Qt::PointingHandCursor);
        openBtn->setToolTip("Open this folder in the system file manager.");
        connect(openBtn, &QPushButton::clicked, this, [this]() {
            const QString d = m_folderEdit->text().trimmed();
            if (d.isEmpty() || !QDir(d).exists()) return;
            QDesktopServices::openUrl(QUrl::fromLocalFile(d));
        });
        l->addWidget(lblw, 1);
        l->addWidget(openBtn);
        lbl->addWidget(row);
    }
    lbl->addLayout(folderRow);
    lbl->addWidget(m_recursiveCheck);
    lbl->addSpacing(8);
    lbl->addWidget(m_positionLbl);
    lbl->addLayout(navRow);
    lbl->addSpacing(8);
    lbl->addWidget(m_deleteBtn);
    lbl->addWidget(m_sendToBatchBtn);
    lbl->addStretch();

    ll->addWidget(makeSectionHeader(leftPanel, "FOLDER"));
    ll->addWidget(leftBody, 1);

    // ── Middle column (large centered image preview) ────────────────────────
    auto* middlePanel = new QWidget(this);
    auto* ml = new QVBoxLayout(middlePanel);
    ml->setContentsMargins(0, 0, 0, 0);
    ml->setSpacing(0);

    auto* middleBody = new QWidget(middlePanel);
    auto* mbl = new QVBoxLayout(middleBody);
    mbl->setContentsMargins(16, 16, 16, 16);
    mbl->setSpacing(8);

    m_focusImage = new QLabel(middleBody);
    m_focusImage->setObjectName("DatasetPreviewImage");
    m_focusImage->setAttribute(Qt::WA_StyledBackground, true);
    m_focusImage->setAlignment(Qt::AlignCenter);
    m_focusImage->setMinimumHeight(360);
    m_focusImage->setCursor(Qt::PointingHandCursor);
    m_focusImage->installEventFilter(this);
    m_focusImage->setToolTip("Click to open the image in the system viewer.");
    // Expanding/Expanding fills the middle column. The pixmap-driven
    // natural width would otherwise lock the column, so let it shrink.
    auto sp = m_focusImage->sizePolicy();
    sp.setHorizontalPolicy(QSizePolicy::Expanding);
    sp.setVerticalPolicy(QSizePolicy::Expanding);
    sp.setHeightForWidth(false);
    m_focusImage->setSizePolicy(sp);
    m_focusImage->setMinimumWidth(0);

    mbl->addWidget(m_focusImage, 1);

    // Inline header (title left, filename right) since makeSectionHeader
    // doesn't support a subtitle.
    auto* imageHeader = new QWidget(middlePanel);
    imageHeader->setObjectName("DatasetSectionHeader");
    imageHeader->setAttribute(Qt::WA_StyledBackground, true);
    imageHeader->setFixedHeight(50);
    {
        auto* hl = new QHBoxLayout(imageHeader);
        hl->setContentsMargins(16, 12, 16, 12);
        hl->setSpacing(12);

        auto* title = new QLabel("IMAGE", imageHeader);
        title->setObjectName("DatasetSectionTitle");

        m_imageNameLbl = new QLabel(imageHeader);
        m_imageNameLbl->setObjectName("DatasetSectionSubtitle");
        m_imageNameLbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        // Elide on the right so very long filenames don't push past the panel.
        m_imageNameLbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        hl->addWidget(title);
        hl->addWidget(m_imageNameLbl, 1);
    }

    ml->addWidget(imageHeader);
    ml->addWidget(middleBody, 1);

    // ── Right column (search bar + editor + highlight) ──────────────────────
    auto* rightPanel = new QWidget(this);
    rightPanel->setObjectName("DatasetPreviewPanel");
    rightPanel->setAttribute(Qt::WA_StyledBackground, true);
    rightPanel->setFixedWidth(kRightPanelWidth);

    auto* rl = new QVBoxLayout(rightPanel);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);

    auto* rightBody = new QWidget(rightPanel);
    auto* rbl = new QVBoxLayout(rightBody);
    rbl->setContentsMargins(12, 12, 12, 12);
    rbl->setSpacing(8);

    m_tagSearchBar = new TagSearchBar(rightBody);
    m_tagSearchBar->setIndex(m_danbooruIndex);
    m_tagSearchBar->setActiveTags(&m_activeCanonical);

    m_tagEdit = new QPlainTextEdit(rightBody);
    m_tagEdit->setObjectName("DatasetExcludeEdit");
    m_tagEdit->setPlaceholderText("Pick an image to edit its tags…");
    m_tagEdit->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));
    m_tagEdit->setEnabled(false);

    // Both use #DatasetExcludeEdit so backgrounds, borders, and font
    // sizes line up with the tag-edit area below.
    m_highlightEdit = new QLineEdit(rightBody);
    m_highlightEdit->setObjectName("DatasetExcludeEdit");
    m_highlightEdit->setPlaceholderText("Highlight (comma-separated)…");
    m_highlightEdit->setClearButtonEnabled(true);
    m_highlightEdit->setFixedHeight(kFolderRowHeight);
    m_highlightEdit->setToolTip(
        "Comma-separated list of substrings. Each token gets its own colour\n"
        "in the editor so distinct matches are visually separable. Case-\n"
        "insensitive. Empty = no highlight.");

    m_editStatus = new QLabel(rightBody);
    m_editStatus->setObjectName("DatasetStatusLabel");

    rbl->addWidget(mkLabel("Add tag"));
    rbl->addWidget(m_tagSearchBar);
    rbl->addWidget(m_tagEdit, 1);
    rbl->addWidget(mkLabel("Highlight"));
    rbl->addWidget(m_highlightEdit);
    rbl->addWidget(m_editStatus);

    rl->addWidget(makeSectionHeader(rightPanel, "EDITOR"));
    rl->addWidget(rightBody, 1);

    m_highlighter = new TagSearchHighlighter(m_tagEdit->document());

    // ── Root ────────────────────────────────────────────────────────────────
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(leftPanel);
    root->addWidget(middlePanel, 1);
    root->addWidget(rightPanel);

    // ── Hydrate from settings ────────────────────────────────────────────────
    if (m_settings && !m_settings->tagEditorFolder.isEmpty()) {
        m_folderEdit->setText(m_settings->tagEditorFolder);
        rescan();
    }

    // ── Wire ────────────────────────────────────────────────────────────────
    connect(m_browseBtn, &QPushButton::clicked, this, [this]() {
        const QString d =
            QFileDialog::getExistingDirectory(this, "Choose folder", m_folderEdit->text());
        if (!d.isEmpty()) {
            m_folderEdit->setText(d);
            persistSettings();
            rescan();
        }
    });
    connect(m_folderEdit, &QLineEdit::editingFinished, this, [this]() {
        persistSettings();
        rescan();
    });
    connect(m_recursiveCheck, &QCheckBox::toggled, this, [this](bool) { rescan(); });

    connect(m_highlightEdit, &QLineEdit::textChanged, this, [this](const QString& t) {
        if (m_highlighter) m_highlighter->setPattern(t);
    });

    // Arrow-key nav scoped with WidgetWithChildrenShortcut so it doesn't
    // steal arrows from text fields.
    {
        auto* prevSc = new QShortcut(QKeySequence(Qt::Key_Left), leftPanel);
        prevSc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(prevSc, &QShortcut::activated, this, [this]() { jumpTo(m_currentIndex - 1); });

        auto* nextSc = new QShortcut(QKeySequence(Qt::Key_Right), leftPanel);
        nextSc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(nextSc, &QShortcut::activated, this, [this]() { jumpTo(m_currentIndex + 1); });
    }

    connect(m_navFirstBtn, &QPushButton::clicked, this, [this]() { jumpTo(0); });
    connect(m_navPrev10Btn, &QPushButton::clicked, this, [this]() { jumpTo(m_currentIndex - 10); });
    connect(m_navPrevBtn, &QPushButton::clicked, this, [this]() { jumpTo(m_currentIndex - 1); });
    connect(m_navNextBtn, &QPushButton::clicked, this, [this]() { jumpTo(m_currentIndex + 1); });
    connect(m_navNext10Btn, &QPushButton::clicked, this, [this]() { jumpTo(m_currentIndex + 10); });
    connect(m_navLastBtn, &QPushButton::clicked, this, [this]() { jumpTo(m_images.size() - 1); });

    connect(m_deleteBtn, &QPushButton::clicked, this, &TagEditorPage::onDeleteClicked);

    connect(m_sendToBatchBtn, &QPushButton::clicked, this, [this]() {
        const QString f = m_folderEdit->text().trimmed();
        if (f.isEmpty()) return;
        emit sendToBatchEditRequested(f);
    });

    // Disable "Send to Batch Edit" while the folder field is empty so the
    // user can't fire a no-op handoff. Tracks the line edit live.
    auto syncSendBatch = [this]() {
        m_sendToBatchBtn->setEnabled(!m_folderEdit->text().trimmed().isEmpty());
    };
    syncSendBatch();
    connect(m_folderEdit, &QLineEdit::textChanged, this,
            [syncSendBatch](const QString&) { syncSendBatch(); });

    connect(m_tagEdit, &QPlainTextEdit::textChanged, this, [this]() {
        rebuildActiveTags();
        scheduleSave();
    });

    connect(m_tagSearchBar, &TagSearchBar::tagAdded, this, &TagEditorPage::onSearchBarTagAdded);
}

TagEditorPage::~TagEditorPage()
{
    // Flush any pending edits before tear-down so we don't lose changes.
    if (m_saveTimer && m_saveTimer->isActive()) {
        m_saveTimer->stop();
        saveNow();
    }
}

void TagEditorPage::setInputFolder(const QString& folder)
{
    if (m_folderEdit) m_folderEdit->setText(folder);
    persistSettings();
    rescan();
}

void TagEditorPage::setDanbooruIndex(core::DanbooruIndex* index)
{
    m_danbooruIndex = index;
    if (m_tagSearchBar) m_tagSearchBar->setIndex(index);
}

// ── Folder rescan ────────────────────────────────────────────────────────────

void TagEditorPage::rescan()
{
    // Drop any pending save before we forget the current path.
    if (m_saveTimer && m_saveTimer->isActive()) {
        m_saveTimer->stop();
        saveNow();
    }

    m_images.clear();
    m_currentIndex = -1;
    m_currentImagePath.clear();
    m_currentTxtPath.clear();
    m_tagEdit->setPlainText({});
    m_tagEdit->setEnabled(false);
    m_editStatus->clear();
    m_focusPixmapSrc = {};
    m_focusImage->clear();
    if (m_imageNameLbl) m_imageNameLbl->clear();

    const QString folder = m_folderEdit->text().trimmed();
    if (folder.isEmpty() || !QDir(folder).exists()) {
        updateNavigationButtons();
        return;
    }

    QDirIterator::IteratorFlags flags = m_recursiveCheck->isChecked()
                                            ? QDirIterator::Subdirectories
                                            : QDirIterator::NoIteratorFlags;

    QDirIterator it(folder, kImageFilters, QDir::Files, flags);
    while (it.hasNext())
        m_images << it.next();
    std::sort(m_images.begin(), m_images.end());

    if (!m_images.isEmpty())
        jumpTo(0);
    else
        updateNavigationButtons();
}

// ── Navigation ───────────────────────────────────────────────────────────────

void TagEditorPage::jumpTo(int newIndex)
{
    if (m_images.isEmpty()) {
        m_currentIndex = -1;
        m_currentImagePath.clear();
        m_currentTxtPath.clear();
        m_tagEdit->setEnabled(false);
        m_tagEdit->setPlainText({});
        m_focusPixmapSrc = {};
        m_focusImage->clear();
        if (m_imageNameLbl) m_imageNameLbl->clear();
        m_editStatus->setText("No images in folder.");
        updateNavigationButtons();
        return;
    }

    // Flush any pending edits for the previous image first.
    if (m_saveTimer && m_saveTimer->isActive()) {
        m_saveTimer->stop();
        saveNow();
    }

    // Clamp into range - allows callers to do `jumpTo(currentIndex - 10)`
    // without bounds-checking themselves.
    newIndex = std::clamp(newIndex, 0, int(m_images.size()) - 1);

    // External-deletion guard: drop the entry and recurse to the next
    // existing index. The early-return at the top handles empty lists.
    if (!QFileInfo::exists(m_images.at(newIndex))) {
        m_images.removeAt(newIndex);
        if (m_images.isEmpty()) {
            jumpTo(0);
            return;
        }
        if (newIndex >= m_images.size()) newIndex = m_images.size() - 1;
        jumpTo(newIndex);
        return;
    }

    m_currentIndex = newIndex;
    m_currentImagePath = m_images.at(newIndex);
    m_currentTxtPath = sidecarPathForImage(m_currentImagePath);

    QImageReader reader(m_currentImagePath);
    reader.setAutoTransform(true);
    const QImage img = reader.read();
    if (!img.isNull()) {
        // Stash the native-resolution source so we can re-scale on every
        // window resize without re-decoding from disk.
        m_focusPixmapSrc = QPixmap::fromImage(img);
        rescalePreview();
    }
    else {
        m_focusPixmapSrc = {};
        m_focusImage->clear();
        m_focusImage->setText("(image failed to decode)");
    }

    QString contents;
    if (QFile f(m_currentTxtPath); f.open(QIODevice::ReadOnly | QIODevice::Text))
        contents = QString::fromUtf8(f.readAll());

    QSignalBlocker b(m_tagEdit);
    m_tagEdit->setEnabled(true);
    m_tagEdit->setPlainText(contents);

    // Image-section header shows the path relative to the chosen folder so
    // recursive folders surface useful context (subdir/foo.png).
    const QString rel = QDir(m_folderEdit->text().trimmed()).relativeFilePath(m_currentImagePath);
    if (m_imageNameLbl) m_imageNameLbl->setText(rel);

    m_editStatus->setText(QFile::exists(m_currentTxtPath)
                              ? QString("Editing %1").arg(QFileInfo(m_currentTxtPath).fileName())
                              : QString("New: %1").arg(QFileInfo(m_currentTxtPath).fileName()));

    rebuildActiveTags();
    updateNavigationButtons();
}

void TagEditorPage::updateNavigationButtons()
{
    const int total = int(m_images.size());
    const bool any = total > 0;
    const int pos = any ? m_currentIndex + 1 : 0;

    if (m_positionLbl)
        m_positionLbl->setText(any ? QString("%1 / %2").arg(pos).arg(total) : QString("0 / 0"));

    const bool canBack = any && m_currentIndex > 0;
    const bool canFwd = any && m_currentIndex < total - 1;

    if (m_navFirstBtn) m_navFirstBtn->setEnabled(canBack);
    if (m_navPrev10Btn) m_navPrev10Btn->setEnabled(canBack);
    if (m_navPrevBtn) m_navPrevBtn->setEnabled(canBack);
    if (m_navNextBtn) m_navNextBtn->setEnabled(canFwd);
    if (m_navNext10Btn) m_navNext10Btn->setEnabled(canFwd);
    if (m_navLastBtn) m_navLastBtn->setEnabled(canFwd);
    if (m_deleteBtn) m_deleteBtn->setEnabled(any);
}

// ── Delete (recycle bin) ─────────────────────────────────────────────────────

void TagEditorPage::onDeleteClicked()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_images.size()) return;

    const QString imgPath = m_currentImagePath;
    const QString txtPath = m_currentTxtPath;

    // Cancel any pending save on the file we're about to trash so we don't
    // immediately recreate it after deletion.
    if (m_saveTimer && m_saveTimer->isActive()) m_saveTimer->stop();
    m_currentImagePath.clear();
    m_currentTxtPath.clear();

    bool ok = true;
    if (QFile::exists(imgPath) && !QFile::moveToTrash(imgPath)) ok = false;
    if (QFile::exists(txtPath) && !QFile::moveToTrash(txtPath)) ok = false;

    if (!ok) {
        // Restore the path pointers so the user can retry without losing
        // their place.
        m_currentImagePath = imgPath;
        m_currentTxtPath = txtPath;
        m_editStatus->setText("Delete failed (recycle bin unavailable).");
        return;
    }

    // Stay on the same index so the cursor lands on the next image; step
    // back if we were on the last one.
    m_images.removeAt(m_currentIndex);
    if (m_currentIndex >= m_images.size()) --m_currentIndex;

    if (m_images.isEmpty()) {
        m_currentIndex = -1;
        m_tagEdit->setPlainText({});
        m_tagEdit->setEnabled(false);
        m_focusPixmapSrc = {};
        m_focusImage->clear();
        if (m_imageNameLbl) m_imageNameLbl->clear();
        m_editStatus->setText("No images left.");
        updateNavigationButtons();
        return;
    }

    jumpTo(m_currentIndex);
    m_editStatus->setText(QString("Moved %1 to recycle bin.").arg(QFileInfo(imgPath).fileName()));
}

// ── Search bar to editor ────────────────────────────────────────────────────

void TagEditorPage::onSearchBarTagAdded(const QString& canonical)
{
    if (!m_tagEdit->isEnabled()) return;

    // Append in space-form so the editor stays consistent with what AutoTagger
    // wrote - utils::normalizeTagInput strips underscores and trims.
    const QString display = utils::normalizeTagInput(canonical);

    QString text = m_tagEdit->toPlainText().trimmed();
    if (text.isEmpty())
        text = display;
    else
        text += ", " + display;

    m_tagEdit->setPlainText(text);
}

void TagEditorPage::rebuildActiveTags()
{
    // Editor stores tags in the in-app space form; the search-bar membership
    // check works on canonical (underscore) form, so we convert both ways.
    m_activeCanonical.clear();
    for (const QString& part : m_tagEdit->toPlainText().split(',', Qt::SkipEmptyParts)) {
        const QString t = part.trimmed();
        if (t.isEmpty()) continue;
        m_activeCanonical.insert(utils::serializeTagOutput(t));
    }
}

// ── Auto-save ────────────────────────────────────────────────────────────────

void TagEditorPage::scheduleSave()
{
    if (m_currentTxtPath.isEmpty()) return;
    m_saveTimer->start();
}

void TagEditorPage::saveNow()
{
    if (m_currentTxtPath.isEmpty()) return;

    QFile f(m_currentTxtPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        m_editStatus->setText(QString("Save failed: %1").arg(f.errorString()));
        return;
    }
    f.write(m_tagEdit->toPlainText().toUtf8());
    m_editStatus->setText(QString("Saved %1").arg(QFileInfo(m_currentTxtPath).fileName()));
}

void TagEditorPage::persistSettings()
{
    if (!m_settings) return;
    m_settings->tagEditorFolder = m_folderEdit->text().trimmed();
}

// ── Image preview scale ──────────────────────────────────────────────────────

void TagEditorPage::rescalePreview()
{
    if (!m_focusImage) return;
    if (m_focusPixmapSrc.isNull()) return;

    // Scale to the label's current size; aspect ratio kept, smooth
    // transform, rounded clip.
    const QSize area = m_focusImage->size();
    if (area.width() <= 0 || area.height() <= 0) return;

    m_focusImage->setPixmap(roundedScaled(m_focusPixmapSrc, area.width(), area.height(), 8.0));
}

// ── Click image to open ──────────────────────────────────────────────────────

bool TagEditorPage::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == m_focusImage) {
        // resizeEvent on the label tracks "available area changed".
        if (ev->type() == QEvent::Resize) {
            rescalePreview();
            // Don't consume; let the label run its own resize logic too.
        }
        if (ev->type() == QEvent::MouseButtonRelease) {
            auto* me = static_cast<QMouseEvent*>(ev);
            if (me->button() == Qt::LeftButton && !m_currentImagePath.isEmpty() &&
                QFile::exists(m_currentImagePath)) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(m_currentImagePath));
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, ev);
}

} // namespace gui
