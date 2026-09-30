#include <app/output_viewer_page.h>
#include <app/app_scroll_bar.h>
#include <app/icons.h>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSet>
#include <QShowEvent>
#include <QSplitter>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
#include <atomic>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

const QStringList kImageFilters = {u"*.png"_s,  u"*.jpg"_s, u"*.jpeg"_s,
                                   u"*.webp"_s, u"*.bmp"_s, u"*.gif"_s};

// The part before the first brace: the real folder that anchors the tree,
// even when the pattern points into a date-stamped subfolder.
QString stripPatternToRoot(const QString& pattern)
{
    QString root = pattern;
    const qsizetype brace = root.indexOf(u'{');
    if (brace >= 0) root = root.first(brace);

    root = root.trimmed();
    while (root.endsWith(u'/') || root.endsWith(u'\\'))
        root.chop(1);
    return root;
}

// `path/{yyyy-MM-dd}/...` against the current date. Whatever is inside the
// braces goes straight to QDateTime::toString; no braces returns it as-is.
QString evaluateDatePattern(const QString& pattern)
{
    const qsizetype open = pattern.indexOf(u'{');
    if (open < 0) return pattern;

    const qsizetype close = pattern.indexOf(u'}', open);
    if (close < 0) return pattern.first(open);

    return pattern.first(open)
        + QDateTime::currentDateTime().toString(pattern.sliced(open + 1, close - open - 1))
        + pattern.sliced(close + 1);
}

// The platform's own file icons are light-on-light here, so the tree draws
// its own two.
class TypeIconProvider : public QFileIconProvider {
public:
    TypeIconProvider() : m_folder(makeFolder()), m_image(makeImage()) {}

    QIcon icon(IconType type) const override
    {
        switch (type) {
        case Folder:
            return m_folder;
        case File:
            return m_image;
        default:
            return {};
        }
    }

    QIcon icon(const QFileInfo& info) const override
    {
        return info.isDir() ? m_folder : m_image;
    }

private:
    static QIcon makeFolder()
    {
        QPixmap pixmap(32, 32);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0x88, 0x88, 0x88));
        painter.drawRoundedRect(QRectF(4, 6, 12, 5), 2, 2);
        painter.drawRoundedRect(QRectF(2, 9, 28, 18), 3, 3);
        return QIcon(pixmap);
    }

    static QIcon makeImage()
    {
        QPixmap pixmap(32, 32);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(0x88, 0x88, 0x88), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(3, 5, 26, 22), 3, 3);

        QPainterPath peaks;
        peaks.moveTo(6, 24);
        peaks.lineTo(13, 14);
        peaks.lineTo(17, 19);
        peaks.lineTo(22, 12);
        peaks.lineTo(26, 24);
        peaks.closeSubpath();

        painter.setPen(Qt::NoPen);
        painter.fillPath(peaks, QColor(0x88, 0x88, 0x88));
        return QIcon(pixmap);
    }

    QIcon m_folder;
    QIcon m_image;
};

} // namespace

// ---- Tree

class OutputTreeView : public QTreeView {
    Q_OBJECT

public:
    explicit OutputTreeView(QWidget* parent = nullptr) : QTreeView(parent) {}

signals:
    void enterOnFile(const QString& path);
    void enterOnDir(const QModelIndex& index);
    void crossToThumbsRequested(const QString& path);

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        const int key = event->key();
        const bool isEnter = key == Qt::Key_Return || key == Qt::Key_Enter;
        if (!isEnter && key != Qt::Key_Right) {
            QTreeView::keyPressEvent(event);
            return;
        }

        const QModelIndex index = currentIndex();
        auto* model = qobject_cast<QFileSystemModel*>(this->model());
        if (!index.isValid() || !model) {
            QTreeView::keyPressEvent(event);
            return;
        }

        const bool isDir = model->isDir(index);

        // Right on a file crosses into the thumbnail pane, which is how the
        // keyboard moves between the two halves.
        if (key == Qt::Key_Right && !isDir) {
            emit crossToThumbsRequested(model->filePath(index));
            event->accept();
            return;
        }

        if (!isEnter) {
            QTreeView::keyPressEvent(event);
            return;
        }

        if (isDir)
            emit enterOnDir(index);
        else
            emit enterOnFile(model->filePath(index));
        event->accept();
    }
};

// ---- Thumbnail grid

class OutputThumbList : public QListWidget {
    Q_OBJECT

public:
    explicit OutputThumbList(QWidget* parent = nullptr) : QListWidget(parent)
    {
        setObjectName(u"OutputThumbList"_s);
        setViewMode(QListView::IconMode);
        setIconSize(QSize(kThumbWidth, kThumbHeight));
        setGridSize(QSize(kThumbWidth + 24, kThumbHeight + 36));
        setResizeMode(QListView::Adjust);
        setMovement(QListView::Static);
        setUniformItemSizes(true);
        setSpacing(8);
        setWordWrap(true);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBar(new AppScrollBar(Qt::Vertical));
        setFrameShape(QFrame::NoFrame);

        m_folderIcon = makeFolderIcon();

        QPixmap placeholder(kThumbWidth, kThumbHeight);
        placeholder.fill(Qt::transparent);
        m_placeholderIcon = QIcon(placeholder);
    }

    // Each entry is a path and whether it is a directory.
    void setEntries(QList<QPair<QString, bool>> entries)
    {
        // Bumped first, so a decode still in flight for the previous folder
        // cannot write its result into this one.
        const int generation = ++m_generation;
        clear();
        m_pending.clear();

        // Folders first, then images, each alphabetical.
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            if (a.second != b.second) return a.second;
            return QString::localeAwareCompare(a.first, b.first) < 0;
        });

        for (const auto& [path, isDir] : entries) {
            auto* item = new QListWidgetItem(QFileInfo(path).fileName(), this);
            item->setData(Qt::UserRole, path);
            item->setData(Qt::UserRole + 1, isDir);
            item->setToolTip(path);
            item->setSizeHint(QSize(kThumbWidth + 16, kThumbHeight + 32));

            if (isDir) {
                item->setIcon(m_folderIcon);
                continue;
            }
            item->setIcon(m_placeholderIcon);
            requestThumbnail(path, generation);
        }

        // Queued: the items have no laid-out geometry yet this turn.
        QMetaObject::invokeMethod(this, [this]() { centreLayout(); }, Qt::QueuedConnection);
    }

signals:
    // Enter on the focused item; a click only selects.
    void enterActivated(QListWidgetItem* item);
    void escapePressed();

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        const int key = event->key();

        if ((key == Qt::Key_Return || key == Qt::Key_Enter) && currentItem()) {
            emit enterActivated(currentItem());
            event->accept();
            return;
        }
        if (key == Qt::Key_Escape && currentItem()) {
            emit escapePressed();
            event->accept();
            return;
        }
        QListWidget::keyPressEvent(event);
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QListWidget::resizeEvent(event);
        centreLayout();
    }

    void showEvent(QShowEvent* event) override
    {
        QListWidget::showEvent(event);
        centreLayout();
    }

private:
    static constexpr int kThumbWidth = 192;
    static constexpr int kThumbHeight = 192;

    // Icon mode left-aligns its grid, which looks lopsided in a wide pane.
    // The leftover width is split into equal viewport margins instead.
    void centreLayout()
    {
        const int gridWidth = gridSize().width();
        const int gap = spacing();
        if (gridWidth <= 0) return;

        const int scrollbar = (verticalScrollBar() && verticalScrollBar()->isVisible())
            ? verticalScrollBar()->width()
            : 0;

        const int inner = width() - scrollbar;
        if (inner <= gap) return;

        const int columns = qMax(1, (inner - gap) / (gridWidth + gap));
        const int used = columns * (gridWidth + gap) + gap;
        const int side = qMax(0, (inner - used) / 2);
        setViewportMargins(side, 16, side, 0);
    }

    void requestThumbnail(const QString& path, int generation)
    {
        if (m_pending.contains(path)) return;
        m_pending.insert(path);

        // Guarded: the page can be destroyed while decodes are outstanding.
        const QPointer<OutputThumbList> self(this);
        QtConcurrent::run([self, path, generation]() {
            const QImage image(path);

            if (image.isNull()) {
                QMetaObject::invokeMethod(
                    self.data(),
                    [self, path, generation]() {
                        if (!self || self->m_generation != generation) return;
                        self->m_pending.remove(path);
                    },
                    Qt::QueuedConnection);
                return;
            }

            QImage scaled = image.scaled(QSize(kThumbWidth, kThumbHeight), Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);

            QMetaObject::invokeMethod(
                self.data(),
                [self, path, generation, scaled = std::move(scaled)]() {
                    if (!self || self->m_generation != generation) return;
                    self->applyThumbnail(path, QPixmap::fromImage(scaled));
                    self->m_pending.remove(path);
                },
                Qt::QueuedConnection);
        });
    }

    void applyThumbnail(const QString& path, const QPixmap& pixmap)
    {
        for (int i = 0; i < count(); ++i) {
            QListWidgetItem* entry = item(i);
            if (!entry || entry->data(Qt::UserRole).toString() != path) continue;
            entry->setIcon(QIcon(pixmap));
            return;
        }
    }

    // A tile-sized folder glyph, so a directory is not a blank square.
    QIcon makeFolderIcon() const
    {
        QPixmap pixmap(kThumbWidth, kThumbHeight);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHints(QPainter::Antialiasing);

        QPainterPath background;
        background.addRoundedRect(QRectF(0, 0, kThumbWidth, kThumbHeight), 12, 12);
        painter.fillPath(background, QColor(28, 28, 28));

        const qreal centreX = kThumbWidth / 2.0;
        const qreal centreY = kThumbHeight / 2.0;
        const qreal width = kThumbWidth * 0.55;
        const qreal height = kThumbHeight * 0.40;

        const QRectF body(centreX - width / 2, centreY - height / 2 + 6, width, height);
        const QRectF tab(centreX - width / 2, centreY - height / 2 - 8, width * 0.45, 16);

        QPainterPath folder;
        folder.addRoundedRect(tab, 4, 4);
        folder.addRoundedRect(body, 6, 6);
        painter.fillPath(folder, QColor(204, 204, 204));

        painter.setPen(QPen(QColor(238, 238, 238, 180), 2));
        painter.drawLine(QPointF(body.left() + 8, body.top() + 4),
                         QPointF(body.right() - 8, body.top() + 4));
        return QIcon(pixmap);
    }

    std::atomic<int> m_generation{0};
    QSet<QString> m_pending;
    QIcon m_folderIcon;
    QIcon m_placeholderIcon;
};

// ---- Page

OutputViewerPage::OutputViewerPage(QWidget* parent) : QWidget(parent)
{
    setObjectName(u"OutputViewerPage"_s);
    setAttribute(Qt::WA_StyledBackground, true);

    m_model = new QFileSystemModel(this);

    // Static: the model keeps a bare pointer to its provider.
    static TypeIconProvider iconProvider;
    m_model->setIconProvider(&iconProvider);
    m_model->setNameFilters(kImageFilters);
    m_model->setNameFilterDisables(false);
    m_model->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::AllDirs);
    m_model->setReadOnly(true);

    connect(m_model, &QFileSystemModel::directoryLoaded, this,
            [this](const QString&) { navigateToPendingIfReady(); });

    m_tree = new OutputTreeView(this);
    m_tree->setModel(m_model);
    m_tree->setHeaderHidden(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setAnimated(true);
    m_tree->setExpandsOnDoubleClick(true);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tree->setVerticalScrollBar(new AppScrollBar(Qt::Vertical));
    m_tree->setFrameShape(QFrame::NoFrame);
    for (int column = 1; column < m_model->columnCount(); ++column)
        m_tree->hideColumn(column);

    m_thumbs = new OutputThumbList(this);

    m_status = new QLabel(this);
    m_status->setObjectName(u"OvStatus"_s);
    m_status->setAlignment(Qt::AlignCenter);
    m_status->setWordWrap(true);
    m_status->setText(
        u"Set a ComfyUI output folder in Settings to browse generated images."_s);

    auto buildHeader = [](const QString& title, QLabel** subtitleOut,
                          QHBoxLayout** layoutOut) {
        auto* header = new QWidget;
        header->setObjectName(u"OvHeader"_s);
        header->setAttribute(Qt::WA_StyledBackground, true);
        header->setFixedHeight(50);

        auto* layout = new QHBoxLayout(header);
        layout->setContentsMargins(20, 12, 20, 12);
        layout->setSpacing(16);

        auto* titleLabel = new QLabel(title);
        titleLabel->setObjectName(u"OvTitle"_s);

        auto* subtitle = new QLabel;
        subtitle->setObjectName(u"OvSubtitle"_s);

        layout->addWidget(titleLabel);
        layout->addWidget(subtitle, 1);

        *subtitleOut = subtitle;
        *layoutOut = layout;
        return header;
    };

    QHBoxLayout* leftHeaderLayout = nullptr;
    QWidget* leftHeader = buildHeader(u"OUTPUT FOLDERS"_s, &m_treeSubtitle, &leftHeaderLayout);

    QHBoxLayout* rightHeaderLayout = nullptr;
    QWidget* rightHeader = buildHeader(u"PREVIEW"_s, &m_thumbSubtitle, &rightHeaderLayout);

    m_openFolderBtn = new QPushButton;
    m_openFolderBtn->setObjectName(u"SidebarBtn"_s);
    m_openFolderBtn->setFixedSize(24, 24);
    m_openFolderBtn->setIcon(icons::openExternal());
    m_openFolderBtn->setIconSize(QSize(14, 14));
    m_openFolderBtn->setCursor(Qt::PointingHandCursor);
    m_openFolderBtn->setToolTip(u"Open this folder in the file explorer"_s);
    m_openFolderBtn->setEnabled(false);
    rightHeaderLayout->addWidget(m_openFolderBtn);

    connect(m_openFolderBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentThumbDir.isEmpty()) return;
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_currentThumbDir));
    });

    m_treeSubtitle->setText(u"(not configured)"_s);

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

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(u"OvSplit"_s);
    m_splitter->addWidget(leftPanel);
    m_splitter->addWidget(rightPanel);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({320, 900});
    m_splitter->setHandleWidth(5);
    m_splitter->setChildrenCollapsible(false);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(m_splitter, 1);
    root->addWidget(m_status);

    connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &OutputViewerPage::onTreeCurrentChanged);
    connect(m_tree, &QAbstractItemView::activated, this, &OutputViewerPage::onTreeActivated);
    connect(m_tree, &OutputTreeView::enterOnFile, this, &OutputViewerPage::openInSystemViewer);
    connect(m_tree, &OutputTreeView::enterOnDir, this, [this](const QModelIndex& index) {
        m_tree->setExpanded(index, !m_tree->isExpanded(index));
    });
    connect(m_tree, &OutputTreeView::crossToThumbsRequested, this,
            &OutputViewerPage::focusThumbForImage);

    connect(m_thumbs, &OutputThumbList::enterActivated, this,
            &OutputViewerPage::onThumbActivated);

    // Escape crosses back, taking the tree's selection with it so the two
    // panes do not disagree about what is current.
    connect(m_thumbs, &OutputThumbList::escapePressed, this, [this]() {
        if (QListWidgetItem* item = m_thumbs->currentItem()) {
            const QString path = item->data(Qt::UserRole).toString();
            const QModelIndex index = path.isEmpty() ? QModelIndex() : m_model->index(path);
            if (index.isValid()) {
                m_tree->setCurrentIndex(index);
                m_tree->scrollTo(index);
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
        m_openFolderBtn->setEnabled(false);
        m_treeSubtitle->setText(m_root.isEmpty() ? u"(not configured)"_s : u"(missing)"_s);
        m_thumbSubtitle->clear();

        m_status->setVisible(true);
        m_status->setText(
            m_root.isEmpty()
                ? u"Set a ComfyUI output folder in Settings to browse generated images."_s
                : u"Output folder does not exist: %1"_s.arg(m_root));
        return;
    }

    m_status->setVisible(false);
    m_treeSubtitle->setText(m_root);
    m_model->setRootPath(m_root);
    m_tree->setRootIndex(m_model->index(m_root));

    // Today's dated folder is where the interesting output is, so it opens
    // there when it exists; otherwise the root does.
    const QString dated = evaluateDatePattern(folderPattern);
    const QFileInfo datedInfo(dated);

    if (dated != m_root && datedInfo.exists() && datedInfo.isDir()) {
        m_pendingNavTo = dated;
        m_currentThumbDir = dated;
        populateThumbsForDir(dated);
        navigateToPendingIfReady();
        return;
    }

    m_currentThumbDir = m_root;
    populateThumbsForDir(m_root);
}

void OutputViewerPage::navigateToPendingIfReady()
{
    if (m_pendingNavTo.isEmpty()) return;

    const QModelIndex index = m_model->index(m_pendingNavTo);
    if (!index.isValid() || !m_model->isDir(index)) return;

    selectInTree(m_pendingNavTo);
    m_pendingNavTo.clear();
}

void OutputViewerPage::onTreeCurrentChanged(const QModelIndex& current, const QModelIndex&)
{
    if (!current.isValid()) return;

    const QString path = m_model->filePath(current);
    const QString directory =
        m_model->isDir(current) ? path : QFileInfo(path).absolutePath();

    // Arrowing between siblings of one folder should not rebuild the grid.
    if (directory == m_currentThumbDir) return;

    m_currentThumbDir = directory;
    populateThumbsForDir(directory);
}

void OutputViewerPage::onTreeActivated(const QModelIndex& index)
{
    if (!index.isValid() || m_model->isDir(index)) return;
    openInSystemViewer(m_model->filePath(index));
}

void OutputViewerPage::focusThumbForImage(const QString& imagePath)
{
    const QString parent = QFileInfo(imagePath).absolutePath();
    if (parent != m_currentThumbDir) {
        m_currentThumbDir = parent;
        populateThumbsForDir(parent);
    }

    for (int i = 0; i < m_thumbs->count(); ++i) {
        QListWidgetItem* item = m_thumbs->item(i);
        if (!item || item->data(Qt::UserRole).toString() != imagePath) continue;

        m_thumbs->setCurrentItem(item);
        m_thumbs->scrollToItem(item);
        m_thumbs->setFocus();
        return;
    }

    // The grid may not hold the file after all, but focus still crosses over.
    m_thumbs->setFocus();
}

void OutputViewerPage::onThumbActivated(QListWidgetItem* item)
{
    if (!item) return;

    const QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty()) return;

    if (item->data(Qt::UserRole + 1).toBool())
        selectInTree(path);
    else
        openInSystemViewer(path);
}

void OutputViewerPage::populateThumbsForDir(const QString& dirPath)
{
    const QDir directory(dirPath);
    if (!directory.exists()) {
        m_thumbs->setEntries({});
        m_thumbSubtitle->clear();
        m_openFolderBtn->setEnabled(false);
        return;
    }
    m_openFolderBtn->setEnabled(true);

    QList<QPair<QString, bool>> entries;
    int folderCount = 0;
    int imageCount = 0;

    for (const QFileInfo& info : directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                         QDir::Name | QDir::IgnoreCase)) {
        entries.append({info.absoluteFilePath(), true});
        ++folderCount;
    }
    for (const QFileInfo& info :
         directory.entryInfoList(kImageFilters, QDir::Files, QDir::Name | QDir::IgnoreCase)) {
        entries.append({info.absoluteFilePath(), false});
        ++imageCount;
    }

    m_thumbs->setEntries(std::move(entries));

    const QString name = QFileInfo(dirPath).fileName().isEmpty()
        ? dirPath
        : QFileInfo(dirPath).fileName();

    m_thumbSubtitle->setText(u"%1 - %2 image%3, %4 folder%5"_s.arg(name)
                                 .arg(imageCount)
                                 .arg(imageCount == 1 ? QString() : u"s"_s)
                                 .arg(folderCount)
                                 .arg(folderCount == 1 ? QString() : u"s"_s));
}

void OutputViewerPage::selectInTree(const QString& path)
{
    const QModelIndex index = m_model->index(path);
    if (!index.isValid()) return;

    // Every ancestor has to be expanded, or scrollTo has nothing to scroll to.
    for (QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent())
        m_tree->expand(parent);

    m_tree->setCurrentIndex(index);
    m_tree->scrollTo(index);
    m_tree->setExpanded(index, true);
}

void OutputViewerPage::openInSystemViewer(const QString& path)
{
    if (path.isEmpty()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

} // namespace tc

#include "output_viewer_page.moc"
