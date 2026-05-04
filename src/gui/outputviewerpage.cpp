#include <gui/outputviewerpage.h>
#include <gui/widgets/appscrollbar.h>
#include <QFileIconProvider>
#include <QFileSystemModel>
#include <QTreeView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QKeyEvent>
#include <QFileInfo>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QDesktopServices>
#include <QUrl>
#include <QtConcurrent>
#include <QPointer>
#include <QLabel>
#include <QSet>
#include <QDateTime>
#include <atomic>
#include <algorithm>

namespace gui {

// ── Image extensions accepted in tree + thumb pane ───────────────────────────
static const QStringList kImageFilters = {
    "*.png", "*.jpg", "*.jpeg", "*.webp", "*.bmp", "*.gif"
};

// Returns the part of `pattern` before the first `{` - the on-disk root that
// should anchor the tree, even when the user's pattern points at a date-stamped
// subfolder ComfyUI is configured to write into.
static QString stripPatternToRoot(const QString& pattern)
{
    QString root = pattern;
    const int brace = root.indexOf(QLatin1Char('{'));
    if (brace >= 0) root = root.left(brace);
    root = root.trimmed();
    while (root.endsWith('/') || root.endsWith('\\')) root.chop(1);
    return root;
}

// Evaluate a `path/{yyyy-MM-dd}/...` pattern against the current date/time:
// the substring inside `{...}` is fed straight into QDateTime::toString. With
// no braces the pattern is returned unchanged.
static QString evaluateDatePattern(const QString& pattern)
{
    const int open = pattern.indexOf(QLatin1Char('{'));
    if (open < 0) return pattern;
    const int close = pattern.indexOf(QLatin1Char('}'), open);
    if (close < 0) return pattern.left(open);
    const QString prefix = pattern.left(open);
    const QString fmt    = pattern.mid(open + 1, close - open - 1);
    const QString suffix = pattern.mid(close + 1);
    const QString dated  = QDateTime::currentDateTime().toString(fmt);
    return prefix + dated + suffix;
}

// ── FsTypeIconProvider ───────────────────────────────────────────────────────
// QFileSystemModel uses QFileIconProvider to source icons; the default hands
// back Windows shell icons (or the Linux/macOS equivalents) which clash with
// the painted-not-system look of the rest of the app. This provider returns
// two simple painted glyphs - folder for directories, frame-with-mountain
// for files - both in the muted-grey palette. Painted at 32×32 so the tree's
// 16×16 display stays sharp on HiDPI screens.
class FsTypeIconProvider : public QFileIconProvider {
public:
    FsTypeIconProvider()
        : m_folder(makeFolder()),
          m_image(makeImage())
    {}

    QIcon icon(IconType t) const override {
        switch (t) {
        case Folder: return m_folder;
        case File:   return m_image;
        default:     return {};
        }
    }
    QIcon icon(const QFileInfo& fi) const override {
        return fi.isDir() ? m_folder : m_image;
    }

private:
    static QIcon makeFolder() {
        QPixmap pm(32, 32);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x88, 0x88, 0x88));
        // Tab on the upper-left, then the body - both rounded so the folder
        // reads as one shape rather than two stacked rectangles.
        p.drawRoundedRect(QRectF(4, 6, 12, 5), 2, 2);
        p.drawRoundedRect(QRectF(2, 9, 28, 18), 3, 3);
        return QIcon(pm);
    }

    static QIcon makeImage() {
        QPixmap pm(32, 32);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        // Frame outline.
        p.setPen(QPen(QColor(0x88, 0x88, 0x88), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(3, 5, 26, 22), 3, 3);
        // "Mountain range" inside - universal shorthand for an image. Filled
        // rather than stroked so it stays readable when downscaled to 16×16.
        p.setPen(Qt::NoPen);
        QPainterPath mtn;
        mtn.moveTo( 6, 24);
        mtn.lineTo(13, 14);
        mtn.lineTo(17, 19);
        mtn.lineTo(22, 12);
        mtn.lineTo(26, 24);
        mtn.closeSubpath();
        p.fillPath(mtn, QColor(0x88, 0x88, 0x88));
        return QIcon(pm);
    }

    QIcon m_folder;
    QIcon m_image;
};

// ── OutputTreeView ───────────────────────────────────────────────────────────
// QTreeView subclass that overrides Enter to:
//   - Toggle expand/collapse on a folder
//   - Open an image file with the OS's default viewer
// All other keys (Up/Down/Left/Right) keep the QTreeView default semantics
// (Right expands a collapsed folder, Left collapses an expanded one).
class OutputTreeView : public QTreeView {
    Q_OBJECT
public:
    explicit OutputTreeView(QWidget* parent = nullptr) : QTreeView(parent) {}

signals:
    void enterOnFile(const QString& path);
    void enterOnDir(const QModelIndex& index);
    // Right arrow on a file row - request that the page move focus into the
    // thumb pane and select the matching item there. (Right on a folder
    // keeps QTreeView's default expand-or-descend behaviour.)
    void crossToThumbsRequested(const QString& path);

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        const int key = event->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter ||
            key == Qt::Key_Right)
        {
            const QModelIndex idx = currentIndex();
            if (idx.isValid()) {
                auto* fs = qobject_cast<QFileSystemModel*>(model());
                if (fs) {
                    const bool dir = fs->isDir(idx);
                    if (key == Qt::Key_Right && !dir) {
                        emit crossToThumbsRequested(fs->filePath(idx));
                        event->accept();
                        return;
                    }
                    if ((key == Qt::Key_Return || key == Qt::Key_Enter)) {
                        if (dir) emit enterOnDir(idx);
                        else     emit enterOnFile(fs->filePath(idx));
                        event->accept();
                        return;
                    }
                }
            }
        }
        QTreeView::keyPressEvent(event);
    }
};

// ── OutputThumbList ──────────────────────────────────────────────────────────
// QListWidget in IconMode that asynchronously loads thumbnails for the items
// currently displayed. A monotonically increasing generation counter cancels
// in-flight loads when the folder changes - late-arriving results compare
// their generation against the current one and bail out if stale.
class OutputThumbList : public QListWidget {
    Q_OBJECT
signals:
    // Emitted when the user presses Enter on the focused item. Click does not
    // emit this - the page treats clicks as selection only and Enter as the
    // commit action ("open image" / "navigate to folder in tree").
    void enterActivated(QListWidgetItem* item);

    // Emitted when Escape is pressed while an item is selected. Used by the
    // page to bounce focus back to the tree. Always consumed (even in
    // fullscreen) so the user gets a one-step-at-a-time Escape ladder:
    // right pane → tree → exit fullscreen.
    void escapePressed();

public:
    explicit OutputThumbList(QWidget* parent = nullptr) : QListWidget(parent)
    {
        setViewMode(QListView::IconMode);
        setIconSize(QSize(kThumbW, kThumbH));
        setGridSize(QSize(kThumbW + 24, kThumbH + 36));
        setResizeMode(QListView::Adjust);
        setMovement(QListView::Static);
        setUniformItemSizes(true);
        setSpacing(8);
        setWordWrap(true);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBar(new AppScrollBar(Qt::Vertical));
        setFrameShape(QFrame::NoFrame);
        setObjectName("OutputThumbList");

        // Folder-tile fallback (used as initial icon for directory items, and
        // as the final icon - folders aren't loaded asynchronously).
        m_folderIcon = makeFolderIcon();
        // Transparent placeholder for image items while real thumbnails load.
        QPixmap ph(kThumbW, kThumbH);
        ph.fill(Qt::transparent);
        m_placeholderIcon = QIcon(ph);
    }

protected:
    // Enter is the only key that triggers the "open image / navigate to folder"
    // action; mouse clicks fall through to the default selection-only behaviour.
    // Escape (with a current item) hands focus back to the tree, regardless of
    // fullscreen - see comment on escapePressed for the rationale.
    void keyPressEvent(QKeyEvent* event) override
    {
        const int key = event->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            if (QListWidgetItem* it = currentItem()) {
                emit enterActivated(it);
                event->accept();
                return;
            }
        }
        if (key == Qt::Key_Escape && currentItem()) {
            emit escapePressed();
            event->accept();
            return;
        }
        QListWidget::keyPressEvent(event);
    }

public:
    // Replace contents with `entries`. Bumps generation, so any thumbnail
    // loads spawned for the previous folder will discard their results.
    void setEntries(const QList<QPair<QString, bool>>& entries) // (path, isDir)
    {
        const int gen = ++m_generation;
        clear();
        m_pending.clear();

        // Directories first, alphabetical; then images, alphabetical.
        QList<QPair<QString, bool>> sorted = entries;
        std::sort(sorted.begin(), sorted.end(),
            [](const QPair<QString, bool>& a, const QPair<QString, bool>& b) {
                if (a.second != b.second) return a.second; // dirs first
                return QString::localeAwareCompare(a.first, b.first) < 0;
            });

        for (const auto& [path, isDir] : sorted) {
            const QString name = QFileInfo(path).fileName();
            auto* item = new QListWidgetItem(name, this);
            item->setData(Qt::UserRole, path);
            item->setData(Qt::UserRole + 1, isDir);
            item->setToolTip(path);
            item->setSizeHint(QSize(kThumbW + 16, kThumbH + 32));
            if (isDir) {
                item->setIcon(m_folderIcon);
            } else {
                item->setIcon(m_placeholderIcon);
                requestThumbnail(path, gen);
            }
        }
    }

private:
    static constexpr int kThumbW = 192;
    static constexpr int kThumbH = 192;

    // Concurrently decode `path` at thumb size; on success post the resulting
    // QPixmap back to the GUI thread and find the corresponding item by path.
    // The generation check on entry skips redundant queueing for the same
    // folder; the second check inside the GUI lambda discards stale results.
    void requestThumbnail(const QString& path, int generation)
    {
        if (m_pending.contains(path)) return;
        m_pending.insert(path);

        QPointer<OutputThumbList> self(this);
        QtConcurrent::run([self, path, generation]() {
            QImage img(path);
            if (img.isNull()) {
                QMetaObject::invokeMethod(self.data(), [self, path, generation]() {
                    if (!self || self->m_generation != generation) return;
                    self->m_pending.remove(path);
                }, Qt::QueuedConnection);
                return;
            }
            QImage scaled = img.scaled(
                QSize(kThumbW, kThumbH),
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation);

            QMetaObject::invokeMethod(self.data(),
                [self, path, generation, scaled = std::move(scaled)]() {
                    if (!self || self->m_generation != generation) return;
                    self->applyThumbnail(path, QPixmap::fromImage(scaled));
                    self->m_pending.remove(path);
                }, Qt::QueuedConnection);
        });
    }

    void applyThumbnail(const QString& path, const QPixmap& pix)
    {
        for (int i = 0; i < count(); ++i) {
            QListWidgetItem* it = item(i);
            if (it && it->data(Qt::UserRole).toString() == path) {
                it->setIcon(QIcon(pix));
                return;
            }
        }
    }

    // Generic folder thumb so directory items aren't blank: a rounded
    // dark-grey rectangle with a folder glyph, drawn once at construction.
    QIcon makeFolderIcon() const
    {
        QPixmap pm(kThumbW, kThumbH);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHints(QPainter::Antialiasing);

        // Tile background
        QPainterPath bg;
        bg.addRoundedRect(QRectF(0, 0, kThumbW, kThumbH), 12, 12);
        p.fillPath(bg, QColor(28, 28, 28));

        // Folder glyph (two-rectangle hand-drawn folder)
        const qreal cx = kThumbW / 2.0;
        const qreal cy = kThumbH / 2.0;
        const qreal w  = kThumbW * 0.55;
        const qreal h  = kThumbH * 0.40;
        const QRectF body(cx - w / 2, cy - h / 2 + 6, w, h);
        const QRectF tab (cx - w / 2, cy - h / 2 - 8, w * 0.45, 16);

        QPainterPath fp;
        fp.addRoundedRect(tab, 4, 4);
        fp.addRoundedRect(body, 6, 6);
        p.fillPath(fp, QColor(204, 204, 204));   // #cccccc, app's primary text colour

        // A subtle highlight strip across the top of the body so the folder
        // reads as 3D rather than a flat block.
        p.setPen(QPen(QColor(238, 238, 238, 180), 2));   // slightly brighter, semi-transparent
        p.drawLine(body.left() + 8, body.top() + 4,
                   body.right() - 8, body.top() + 4);
        return QIcon(pm);
    }

    std::atomic<int> m_generation{0};
    QSet<QString>    m_pending;
    QIcon            m_folderIcon;
    QIcon            m_placeholderIcon;
};

// ── OutputViewerPage ─────────────────────────────────────────────────────────

OutputViewerPage::OutputViewerPage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("OutputViewerPage");
    setAttribute(Qt::WA_StyledBackground, true);

    m_fsModel = new QFileSystemModel(this);
    // Stateless provider; one shared instance for every OutputViewerPage is
    // safe and saves the manual-lifetime dance (QFileIconProvider isn't a
    // QObject, so it can't be parented to the model).
    static FsTypeIconProvider s_typeIcons;
    m_fsModel->setIconProvider(&s_typeIcons);
    m_fsModel->setNameFilters(kImageFilters);
    m_fsModel->setNameFilterDisables(false);
    // `QDir::AllDirs` is what tells QFileSystemModel to bypass the name
    // filters for directories - without it, *.png/*.jpg/... would also be
    // applied to folder names and every subfolder would vanish from the tree.
    // (The model's default filter already includes AllDirs; we restate it
    // here so the contract is explicit at the call site.)
    m_fsModel->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::AllDirs);
    m_fsModel->setReadOnly(true);

    // Re-attempt deferred navigation each time a directory finishes loading
    // - if today's date subfolder lives under a path that wasn't yet realised
    // in the model when setOutputFolder ran, this catches up once it is.
    connect(m_fsModel, &QFileSystemModel::directoryLoaded, this,
            [this](const QString&) { navigateToPendingIfReady(); });

    m_tree = new OutputTreeView(this);
    m_tree->setModel(m_fsModel);
    m_tree->setHeaderHidden(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setAnimated(true);
    m_tree->setExpandsOnDoubleClick(true);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tree->setVerticalScrollBar(new AppScrollBar(Qt::Vertical));
    m_tree->setFrameShape(QFrame::NoFrame);
    // QFileSystemModel exposes Name/Size/Type/Date columns; only Name belongs
    // in the navigation tree.
    for (int c = 1; c < m_fsModel->columnCount(); ++c) m_tree->hideColumn(c);

    m_thumbs = new OutputThumbList(this);

    m_status = new QLabel(this);
    m_status->setObjectName("OvStatus");
    m_status->setText("Set a ComfyUI output folder in Settings to browse generated images.");
    m_status->setAlignment(Qt::AlignCenter);
    m_status->setWordWrap(true);

    // ── Header builder ────────────────────────────────────────────────────────
    // Mirrors the pattern from WorkflowEditPage: a fixed-height bar with a
    // bold title on the left and a dimmer subtitle that the page updates as
    // the user navigates. Returns the (header widget, subtitle label) pair so
    // the page can write into the subtitle later.
    // Match WorkflowEditPage's section-header dimensions so flipping between
    // the two pages doesn't shift the title row vertically.
    constexpr int kHeaderHeight = 50;
    auto buildHeader = [&](const QString& title) -> QPair<QWidget*, QLabel*> {
        auto* header = new QWidget;
        header->setObjectName("OvHeader");
        header->setAttribute(Qt::WA_StyledBackground, true);
        header->setFixedHeight(kHeaderHeight);

        auto* lay = new QHBoxLayout(header);
        lay->setContentsMargins(20, 12, 20, 12);
        lay->setSpacing(16);

        auto* titleLabel = new QLabel(title);
        titleLabel->setObjectName("OvTitle");

        auto* subtitle = new QLabel;
        subtitle->setObjectName("OvSubtitle");

        lay->addWidget(titleLabel);
        lay->addWidget(subtitle, 1);
        return { header, subtitle };
    };

    auto [leftHeader,  leftSubtitle ] = buildHeader(QStringLiteral("OUTPUT FOLDERS"));
    auto [rightHeader, rightSubtitle] = buildHeader(QStringLiteral("PREVIEW"));
    m_treeSubtitle  = leftSubtitle;
    m_thumbSubtitle = rightSubtitle;
    // setOutputFolder early-returns when pattern is unchanged, so on the
    // first call with an empty configured path the subtitle would never be
    // initialised - seed it here so the header reads sensibly out of the box.
    m_treeSubtitle->setText(QStringLiteral("(not configured)"));

    auto* leftPanel = new QWidget;
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    leftLayout->addWidget(leftHeader);
    leftLayout->addWidget(m_tree, 1);

    auto* rightPanel = new QWidget;
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    rightLayout->addWidget(rightHeader);
    rightLayout->addWidget(m_thumbs, 1);

    m_split = new QSplitter(Qt::Horizontal, this);
    m_split->setObjectName("OvSplit");
    m_split->addWidget(leftPanel);
    m_split->addWidget(rightPanel);
    m_split->setStretchFactor(0, 0);
    m_split->setStretchFactor(1, 1);
    m_split->setSizes({ 320, 900 });
    // Without a non-trivial handle width the QSS background can't render and
    // the divider stays invisible - 5px is the same width WorkflowEditPage's
    // implicit splitters use.
    m_split->setHandleWidth(5);
    m_split->setChildrenCollapsible(false);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(m_split, 1);
    root->addWidget(m_status);

    connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &OutputViewerPage::onTreeCurrentChanged);
    connect(m_tree, &QAbstractItemView::activated,
            this, &OutputViewerPage::onTreeActivated);
    connect(m_tree, &OutputTreeView::enterOnFile,
            this, &OutputViewerPage::openInSystemViewer);
    connect(m_tree, &OutputTreeView::enterOnDir,
            this, [this](const QModelIndex& idx) {
                m_tree->setExpanded(idx, !m_tree->isExpanded(idx));
            });
    connect(m_tree, &OutputTreeView::crossToThumbsRequested,
            this, &OutputViewerPage::focusThumbForImage);

    // Click in the right pane selects only; Enter (routed through
    // OutputThumbList's keyPressEvent → enterActivated) is the commit action
    // that opens images / navigates folders.
    connect(m_thumbs, &OutputThumbList::enterActivated,
            this, &OutputViewerPage::onThumbActivated);

    // Escape on the right pane (with a selection, not fullscreen) bounces
    // focus back to the tree - sync the tree's current row to whatever the
    // user had highlighted in the thumb grid before handing focus over.
    connect(m_thumbs, &OutputThumbList::escapePressed, this, [this]() {
        if (auto* it = m_thumbs->currentItem()) {
            const QString path = it->data(Qt::UserRole).toString();
            if (!path.isEmpty()) {
                const QModelIndex idx = m_fsModel->index(path);
                if (idx.isValid()) {
                    m_tree->setCurrentIndex(idx);
                    m_tree->scrollTo(idx);
                }
            }
        }
        m_tree->setFocus();
    });
}

void OutputViewerPage::setOutputFolder(const QString& folderPattern)
{
    if (folderPattern == m_pattern) return;
    m_pattern = folderPattern;
    m_pendingNavTo.clear();

    m_root = stripPatternToRoot(folderPattern);

    if (m_root.isEmpty() || !QFileInfo::exists(m_root)) {
        m_tree->setRootIndex(QModelIndex());
        m_thumbs->setEntries({});
        m_currentThumbDir.clear();
        m_treeSubtitle->setText(m_root.isEmpty()
            ? QStringLiteral("(not configured)")
            : QStringLiteral("(missing)"));
        m_thumbSubtitle->clear();
        m_status->setVisible(true);
        m_status->setText(m_root.isEmpty()
            ? QStringLiteral("Set a ComfyUI output folder in Settings to browse generated images.")
            : QStringLiteral("Output folder does not exist: %1").arg(m_root));
        return;
    }

    m_status->setVisible(false);
    m_treeSubtitle->setText(m_root);
    m_fsModel->setRootPath(m_root);
    m_tree->setRootIndex(m_fsModel->index(m_root));

    // Resolve the configured pattern (e.g. `…/output/{yyyy-MM-dd}`) against
    // today's date. If the resulting path exists, focus there straight away;
    // otherwise the user can navigate to it manually from the root.
    const QString datedPath = evaluateDatePattern(folderPattern);
    QFileInfo datedInfo(datedPath);
    const bool hasDated = datedPath != m_root
                       && datedInfo.exists()
                       && datedInfo.isDir();

    if (hasDated) {
        m_pendingNavTo = datedPath;
        m_currentThumbDir = datedPath;
        populateThumbsForDir(datedPath);
        // QFileSystemModel populates lazily; the dated index may not be
        // realised yet. Try once now and retry from directoryLoaded if it
        // wasn't ready (see ctor's signal hookup).
        navigateToPendingIfReady();
    } else {
        m_currentThumbDir = m_root;
        populateThumbsForDir(m_root);
    }
}

void OutputViewerPage::navigateToPendingIfReady()
{
    if (m_pendingNavTo.isEmpty()) return;
    const QModelIndex idx = m_fsModel->index(m_pendingNavTo);
    if (!idx.isValid() || !m_fsModel->isDir(idx)) return;
    selectInTree(m_pendingNavTo);
    m_pendingNavTo.clear();
}

void OutputViewerPage::onTreeCurrentChanged(const QModelIndex& current, const QModelIndex&)
{
    if (!current.isValid()) return;
    const QString path = m_fsModel->filePath(current);
    // For dirs, the right pane mirrors the dir's contents. For files, it
    // mirrors the parent dir - that way arrowing between sibling images in
    // the same folder doesn't change the displayed dir at all (handled by
    // the equality check below, which keeps the thumb grid stable).
    const QString targetDir = m_fsModel->isDir(current)
        ? path
        : QFileInfo(path).absolutePath();

    if (targetDir == m_currentThumbDir) return;
    m_currentThumbDir = targetDir;
    populateThumbsForDir(targetDir);
}

void OutputViewerPage::onTreeActivated(const QModelIndex& index)
{
    if (!index.isValid()) return;
    if (!m_fsModel->isDir(index))
        openInSystemViewer(m_fsModel->filePath(index));
}

void OutputViewerPage::focusThumbForImage(const QString& imagePath)
{
    // The right pane should already be showing this file's parent dir
    // (onTreeCurrentChanged keeps it in sync), but rebuild defensively if
    // something has drifted - otherwise the lookup below would silently fail.
    const QString parentDir = QFileInfo(imagePath).absolutePath();
    if (parentDir != m_currentThumbDir) {
        m_currentThumbDir = parentDir;
        populateThumbsForDir(parentDir);
    }

    for (int i = 0; i < m_thumbs->count(); ++i) {
        QListWidgetItem* it = m_thumbs->item(i);
        if (it && it->data(Qt::UserRole).toString() == imagePath) {
            m_thumbs->setCurrentItem(it);
            m_thumbs->scrollToItem(it);
            m_thumbs->setFocus();
            return;
        }
    }
    // Fallback: thumb pane doesn't have the file (race during repopulate);
    // still move focus so the user's keyboard input lands somewhere useful.
    m_thumbs->setFocus();
}

void OutputViewerPage::onThumbActivated(QListWidgetItem* item)
{
    if (!item) return;
    const QString path  = item->data(Qt::UserRole).toString();
    const bool    isDir = item->data(Qt::UserRole + 1).toBool();
    if (path.isEmpty()) return;
    if (isDir)
        selectInTree(path);
    else
        openInSystemViewer(path);
}

void OutputViewerPage::populateThumbsForDir(const QString& dirPath)
{
    QList<QPair<QString, bool>> entries;
    QDir d(dirPath);
    if (!d.exists()) {
        m_thumbs->setEntries(entries);
        m_thumbSubtitle->clear();
        return;
    }

    int dirCount = 0;
    int imgCount = 0;
    for (const QFileInfo& fi : d.entryInfoList(
             QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase)) {
        entries << qMakePair(fi.absoluteFilePath(), true);
        ++dirCount;
    }
    for (const QFileInfo& fi : d.entryInfoList(
             kImageFilters, QDir::Files, QDir::Name | QDir::IgnoreCase)) {
        entries << qMakePair(fi.absoluteFilePath(), false);
        ++imgCount;
    }
    m_thumbs->setEntries(entries);

    const QString name = QFileInfo(dirPath).fileName().isEmpty()
        ? dirPath
        : QFileInfo(dirPath).fileName();
    m_thumbSubtitle->setText(
        QStringLiteral("%1 - %2 image%3, %4 folder%5")
            .arg(name)
            .arg(imgCount).arg(imgCount == 1 ? "" : "s")
            .arg(dirCount).arg(dirCount == 1 ? "" : "s"));
}

void OutputViewerPage::selectInTree(const QString& path)
{
    const QModelIndex idx = m_fsModel->index(path);
    if (!idx.isValid()) return;
    // Expand all parents up to the root so the index becomes visible - without
    // this, setCurrentIndex on a deep child silently no-ops because the tree
    // hasn't realized those rows yet.
    QModelIndex p = idx.parent();
    while (p.isValid()) {
        m_tree->expand(p);
        p = p.parent();
    }
    m_tree->setCurrentIndex(idx);
    m_tree->scrollTo(idx);
    m_tree->setExpanded(idx, true);
}

void OutputViewerPage::openInSystemViewer(const QString& path)
{
    if (path.isEmpty()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

} // namespace gui

#include "outputviewerpage.moc"
