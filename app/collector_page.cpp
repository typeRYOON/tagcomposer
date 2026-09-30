#include <app/collector_page.h>
#include <app/app_scroll_bar.h>
#include <app/icons.h>
#include <core/settings.h>
#include <tagger/download_watcher.h>
#include <tagger/phash.h>
#include <tagger/phash_index.h>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <tuple>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

const QStringList kImageFilters = {u"*.png"_s, u"*.jpg"_s,  u"*.jpeg"_s,
                                   u"*.webp"_s, u"*.bmp"_s, u"*.gif"_s};

constexpr int kPanelWidth = 380;
constexpr int kRecentPanelWidth = 420;
constexpr int kRowHeight = 40;
constexpr int kHeaderHeight = 50;
constexpr int kThumbSize = 96;

// Newest first, and the tail rolls off. This is a session view, not a
// browser: the collection folder itself is the record.
constexpr int kRecentMax = 50;

// Above this the hash stops describing the same picture and starts merging
// different ones, so the slider stops there rather than letting a drag ruin a
// collection.
constexpr int kMaxThreshold = 16;

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

QString timestamp()
{
    return QDateTime::currentDateTime().toString(u"HH:mm:ss"_s);
}

} // namespace

CollectorPage::CollectorPage(Settings& settings, const QString& collectionsRoot, QWidget* parent)
    : QWidget(parent), m_settings(&settings), m_collectionsRoot(collectionsRoot)
{
    setObjectName(u"CollectorPage"_s);
    setAttribute(Qt::WA_StyledBackground, true);

    m_watcher = new DownloadWatcher(this);

    // ---- Left: what to watch, and where it goes
    auto* left = new QWidget;
    left->setObjectName(u"DatasetParamsPanel"_s);
    left->setAttribute(Qt::WA_StyledBackground, true);
    left->setFixedWidth(kPanelWidth);

    auto* leftBody = new QWidget;
    auto* leftLayout = new QVBoxLayout(leftBody);
    leftLayout->setContentsMargins(12, 12, 12, 12);
    leftLayout->setSpacing(8);

    m_watchEdit = new QLineEdit;
    m_watchEdit->setObjectName(u"SearchBar"_s);
    m_watchEdit->setPlaceholderText(u"Folder to watch (usually Downloads)"_s);
    m_watchEdit->setFixedHeight(kRowHeight);
    m_watchEdit->setToolTip(u"Polled for new images. Each one is hashed and either moved into\n"
                            u"the collection under a sequential name, or recycled when it\n"
                            u"duplicates something already there."_s);

    m_watchBrowse = new QPushButton(u"..."_s);
    m_watchBrowse->setObjectName(u"DatasetBrowseBtn"_s);
    m_watchBrowse->setFixedSize(36, kRowHeight);
    m_watchBrowse->setCursor(Qt::PointingHandCursor);
    m_watchBrowse->setToolTip(u"Pick the watch folder."_s);

    auto* watchRow = new QHBoxLayout;
    watchRow->setContentsMargins(0, 0, 0, 0);
    watchRow->setSpacing(4);
    watchRow->addWidget(m_watchEdit, 1);
    watchRow->addWidget(m_watchBrowse);

    m_collections = new QComboBox;
    m_collections->setObjectName(u"DatasetSpin"_s);
    m_collections->setFixedHeight(kRowHeight);
    m_collections->setToolTip(u"Where new images land, renamed 00001.png, 00002.jpg and so on.\n"
                              u"Each collection keeps its own __hashes.json, so duplicate\n"
                              u"detection picks up where it left off."_s);

    m_newCollection = new QPushButton(u"New"_s);
    m_newCollection->setObjectName(u"DatasetBrowseBtn"_s);
    m_newCollection->setFixedSize(56, kRowHeight);
    m_newCollection->setCursor(Qt::PointingHandCursor);
    m_newCollection->setToolTip(u"Create a collection folder."_s);

    auto* collectionRow = new QHBoxLayout;
    collectionRow->setContentsMargins(0, 0, 0, 0);
    collectionRow->setSpacing(4);
    collectionRow->addWidget(m_collections, 1);
    collectionRow->addWidget(m_newCollection);

    m_threshold = new QSlider(Qt::Horizontal);
    m_threshold->setObjectName(u"DatasetPmiSlider"_s);
    m_threshold->setRange(0, kMaxThreshold);
    m_threshold->setValue(4);
    m_threshold->setToolTip(u"How many bits two hashes may differ by and still count as the\n"
                            u"same image.\n"
                            u"  0-2  nearly identical: a recompress, a stripped watermark\n"
                            u"  3-6  the same image differently edited, the usual choice\n"
                            u"  7+   looser, and starts merging images that are merely alike"_s);

    m_thresholdValue = new QLabel;
    m_thresholdValue->setObjectName(u"DatasetParamValue"_s);
    m_thresholdValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_thresholdValue->setMinimumWidth(28);

    auto syncThreshold = [this]() {
        m_thresholdValue->setText(QString::number(m_threshold->value()));
    };
    syncThreshold();

    auto* thresholdRow = new QHBoxLayout;
    thresholdRow->setContentsMargins(0, 0, 0, 0);
    thresholdRow->setSpacing(6);
    thresholdRow->addWidget(m_threshold, 1);
    thresholdRow->addWidget(m_thresholdValue);

    m_poll = new QSpinBox;
    m_poll->setObjectName(u"DatasetSpin"_s);
    m_poll->setRange(1, 60);
    m_poll->setValue(5);
    m_poll->setSuffix(u" s"_s);
    m_poll->setToolTip(u"How often the folder is scanned. Shorter is more responsive and\n"
                       u"slightly more CPU."_s);

    m_startStop = new QPushButton(u"Start"_s);
    m_startStop->setObjectName(u"DatasetRunBtn"_s);
    m_startStop->setCursor(Qt::PointingHandCursor);
    m_startStop->setToolTip(u"Begin watching. The app stops the watcher on exit."_s);

    m_openFolder = new QPushButton(u"Open collection"_s);
    m_openFolder->setObjectName(u"DatasetBrowseBtn"_s);
    m_openFolder->setFixedHeight(kRowHeight);
    m_openFolder->setCursor(Qt::PointingHandCursor);
    m_openFolder->setToolTip(u"Show this collection in the file manager."_s);

    m_rebuild = new QPushButton(u"Rebuild index"_s);
    m_rebuild->setObjectName(u"DatasetBrowseBtn"_s);
    m_rebuild->setFixedHeight(kRowHeight);
    m_rebuild->setCursor(Qt::PointingHandCursor);
    m_rebuild->setToolTip(u"Re-hash every image in the collection and write __hashes.json\n"
                          u"again. For when the index was deleted, the folder was edited\n"
                          u"from outside, or duplicates are slipping through."_s);

    m_sendToTagger = new QPushButton(u"Send to Auto-tagger"_s);
    m_sendToTagger->setObjectName(u"EntryActionBtn"_s);
    m_sendToTagger->setCursor(Qt::PointingHandCursor);
    m_sendToTagger->setToolTip(u"Open this collection as the Auto-tagger's input folder."_s);

    auto* grid = new QGridLayout;
    grid->setSpacing(6);
    grid->setColumnStretch(1, 1);
    grid->addWidget(paramLabel(u"Threshold"_s), 0, 0);
    grid->addLayout(thresholdRow, 0, 1);
    grid->addWidget(paramLabel(u"Poll"_s), 1, 0);
    grid->addWidget(m_poll, 1, 1);

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
            const QString folder = m_watchEdit->text().trimmed();
            if (folder.isEmpty() || !QDir(folder).exists()) return;
            QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
        });

        rowLayout->addWidget(paramLabel(u"Watch folder"_s), 1);
        rowLayout->addWidget(open);
        leftLayout->addWidget(row);
    }

    leftLayout->addLayout(watchRow);
    leftLayout->addSpacing(4);
    leftLayout->addWidget(paramLabel(u"Collection"_s));
    leftLayout->addLayout(collectionRow);
    leftLayout->addSpacing(8);
    leftLayout->addLayout(grid);
    leftLayout->addSpacing(8);
    leftLayout->addWidget(m_startStop);
    leftLayout->addWidget(m_openFolder);
    leftLayout->addWidget(m_rebuild);
    leftLayout->addWidget(m_sendToTagger);
    leftLayout->addStretch();

    auto* leftColumn = new QVBoxLayout(left);
    leftColumn->setContentsMargins(0, 0, 0, 0);
    leftColumn->setSpacing(0);
    leftColumn->addWidget(sectionHeader(u"AUTO-COLLECT"_s));
    leftColumn->addWidget(leftBody, 1);

    // ---- Middle: what it has been doing
    auto* middle = new QWidget;
    auto* middleBody = new QWidget;
    auto* middleLayout = new QVBoxLayout(middleBody);
    middleLayout->setContentsMargins(12, 12, 12, 12);
    middleLayout->setSpacing(8);

    auto* dot = new QLabel;
    dot->setObjectName(u"CollectorActiveDot"_s);

    m_runState = new QLabel(u"idle"_s);
    m_runState->setObjectName(u"DatasetStatusLabel"_s);
    m_runState->setAlignment(Qt::AlignCenter);

    auto* runBox = new QWidget;
    auto* runLayout = new QHBoxLayout(runBox);
    runLayout->setContentsMargins(0, 0, 0, 0);
    runLayout->setSpacing(6);
    runLayout->addWidget(dot);
    runLayout->addWidget(m_runState);

    m_collected = new QLabel;
    m_collected->setObjectName(u"DatasetParamValue"_s);
    m_skipped = new QLabel;
    m_skipped->setObjectName(u"DatasetParamValue"_s);

    auto* statsRow = new QHBoxLayout;
    statsRow->setContentsMargins(0, 0, 0, 0);
    statsRow->setSpacing(16);
    statsRow->addWidget(runBox);
    statsRow->addStretch();
    statsRow->addWidget(m_collected);
    statsRow->addWidget(m_skipped);

    m_log = new QPlainTextEdit;
    m_log->setObjectName(u"DatasetExcludeEdit"_s);
    m_log->setReadOnly(true);
    m_log->setVerticalScrollBar(new AppScrollBar(Qt::Vertical));
    m_log->setPlaceholderText(u"Activity appears here once the watcher is running."_s);

    middleLayout->addLayout(statsRow);
    middleLayout->addWidget(m_log, 1);

    auto* middleColumn = new QVBoxLayout(middle);
    middleColumn->setContentsMargins(0, 0, 0, 0);
    middleColumn->setSpacing(0);
    middleColumn->addWidget(sectionHeader(u"ACTIVITY"_s));
    middleColumn->addWidget(middleBody, 1);

    auto syncCounters = [this]() {
        m_collected->setText(u"Collected: %1"_s.arg(m_watcher->collectedCount()));
        m_skipped->setText(u"Skipped: %1"_s.arg(m_watcher->skippedCount()));
    };
    syncCounters();

    // ---- Right: what it just took
    auto* recentPanel = new QWidget;
    recentPanel->setObjectName(u"DatasetPreviewPanel"_s);
    recentPanel->setAttribute(Qt::WA_StyledBackground, true);
    recentPanel->setFixedWidth(kRecentPanelWidth);

    auto* recentBody = new QWidget;
    auto* recentLayout = new QVBoxLayout(recentBody);
    recentLayout->setContentsMargins(12, 12, 12, 12);
    recentLayout->setSpacing(8);

    m_recent = new QListWidget;
    m_recent->setObjectName(u"DatasetResultsList"_s);
    m_recent->setVerticalScrollBar(new AppScrollBar(Qt::Vertical));
    m_recent->setFrameShape(QFrame::NoFrame);
    m_recent->setViewMode(QListView::IconMode);
    m_recent->setIconSize(QSize(kThumbSize, kThumbSize));
    m_recent->setResizeMode(QListView::Adjust);
    m_recent->setMovement(QListView::Static);
    m_recent->setSpacing(6);
    m_recent->setUniformItemSizes(true);
    m_recent->setWordWrap(true);
    m_recent->setSelectionMode(QAbstractItemView::SingleSelection);
    m_recent->setVisible(false); // until the first thumbnail arrives

    m_recentEmpty = new QLabel(u"Recently collected images appear here."_s);
    m_recentEmpty->setObjectName(u"DatasetEmptyState"_s);
    m_recentEmpty->setAlignment(Qt::AlignCenter);
    m_recentEmpty->setWordWrap(true);

    recentLayout->addWidget(m_recent, 1);
    recentLayout->addWidget(m_recentEmpty, 1);

    auto* recentColumn = new QVBoxLayout(recentPanel);
    recentColumn->setContentsMargins(0, 0, 0, 0);
    recentColumn->setSpacing(0);
    recentColumn->addWidget(sectionHeader(u"RECENT"_s));
    recentColumn->addWidget(recentBody, 1);

    auto openRecent = [](QListWidgetItem* item) {
        if (!item) return;
        const QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty() && QFile::exists(path))
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    };
    connect(m_recent, &QListWidget::itemActivated, this, openRecent);
    connect(m_recent, &QListWidget::itemDoubleClicked, this, openRecent);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(left);
    root->addWidget(middle, 1);
    root->addWidget(recentPanel);

    // ---- Hydrate
    if (!m_settings->collectorWatchFolder.isEmpty())
        m_watchEdit->setText(m_settings->collectorWatchFolder);
    m_threshold->setValue(std::clamp(m_settings->collectorThreshold, 0, kMaxThreshold));
    m_poll->setValue(std::clamp(m_settings->collectorPollSeconds, 1, 60));
    syncThreshold();

    refreshCollections();
    if (const int at = m_collections->findText(m_settings->collectorActiveCollection); at >= 0)
        m_collections->setCurrentIndex(at);

    // ---- Wiring
    connect(m_watchBrowse, &QPushButton::clicked, this, [this]() {
        const QString folder = QFileDialog::getExistingDirectory(this, u"Choose watch folder"_s,
                                                                 m_watchEdit->text());
        if (folder.isEmpty()) return;
        m_watchEdit->setText(folder);
        persistSettings();
    });
    connect(m_watchEdit, &QLineEdit::editingFinished, this, &CollectorPage::persistSettings);
    connect(m_newCollection, &QPushButton::clicked, this, &CollectorPage::newCollection);
    connect(m_startStop, &QPushButton::clicked, this, &CollectorPage::startOrStop);
    connect(m_rebuild, &QPushButton::clicked, this, &CollectorPage::rebuildIndex);

    connect(m_threshold, &QSlider::valueChanged, this, [this, syncThreshold](int) {
        syncThreshold();
        persistSettings();
    });
    connect(m_poll, &QSpinBox::valueChanged, this, [this](int) { persistSettings(); });

    connect(m_openFolder, &QPushButton::clicked, this, [this]() {
        const QString folder = currentCollectionDir();
        if (folder.isEmpty()) return;
        QDir().mkpath(folder);
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    });
    connect(m_sendToTagger, &QPushButton::clicked, this, [this]() {
        const QString folder = currentCollectionDir();
        if (!folder.isEmpty()) emit sendToAutoTaggerRequested(folder);
    });

    // Nothing here means anything without a collection picked, and a fresh
    // install has none until one is made.
    auto syncCollectionButtons = [this]() {
        const bool picked = !m_collections->currentText().isEmpty();
        m_sendToTagger->setEnabled(picked);
        m_openFolder->setEnabled(picked);
        m_rebuild->setEnabled(picked && !m_watcher->isRunning());
    };
    syncCollectionButtons();
    connect(m_collections, &QComboBox::currentTextChanged, this,
            [this, syncCollectionButtons](const QString&) {
                persistSettings();
                syncCollectionButtons();
            });

    connect(m_watcher, &DownloadWatcher::imageMoved, this,
            [this](const QString& from, const QString& to) {
                m_log->appendPlainText(u"[%1] moved %2 to %3"_s.arg(
                    timestamp(), QFileInfo(from).fileName(), QFileInfo(to).fileName()));
                addRecentThumb(to);
            });
    connect(m_watcher, &DownloadWatcher::imageSkipped, this,
            [this](const QString& from, int distance, const QString& match) {
                m_log->appendPlainText(u"[%1] duplicate %2 (distance %3, matches %4), recycled"_s
                                           .arg(timestamp(), QFileInfo(from).fileName())
                                           .arg(distance)
                                           .arg(match));
            });
    connect(m_watcher, &DownloadWatcher::error, this, [this](const QString& message) {
        m_log->appendPlainText(u"[%1] error - %2"_s.arg(timestamp(), message));
    });
    connect(m_watcher, &DownloadWatcher::status, this,
            [syncCounters](int, int) { syncCounters(); });
    connect(m_watcher, &DownloadWatcher::started, this, [this]() {
        m_log->appendPlainText(u"[%1] started"_s.arg(timestamp()));
        setRunningUi(true);
    });
    connect(m_watcher, &DownloadWatcher::stopped, this, [this]() {
        m_log->appendPlainText(u"[%1] stopped"_s.arg(timestamp()));
        setRunningUi(false);
    });
}

CollectorPage::~CollectorPage()
{
    stopWatcher();
}

void CollectorPage::stopWatcher()
{
    m_watcher->stop();
}

void CollectorPage::refreshCollections()
{
    QDir().mkpath(m_collectionsRoot);

    const QString previous = m_collections->currentText();

    QSignalBlocker blocker(m_collections);
    m_collections->clear();
    for (const QFileInfo& info :
         QDir(m_collectionsRoot).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        m_collections->addItem(info.fileName());

    if (previous.isEmpty()) return;
    if (const int at = m_collections->findText(previous); at >= 0)
        m_collections->setCurrentIndex(at);
}

void CollectorPage::newCollection()
{
    bool accepted = false;
    QString name = QInputDialog::getText(this, u"New collection"_s, u"Collection name:"_s,
                                         QLineEdit::Normal, QString(), &accepted);
    if (!accepted) return;

    name = name.trimmed();
    if (name.isEmpty()) return;

    // This becomes a directory name, and the point is to refuse path
    // traversal before mkpath turns it into one. Anything else the OS objects
    // to shows up as a failed mkpath below.
    static const QString forbidden = u"\\/:*?\"<>|"_s;
    for (const QChar c : forbidden) {
        if (!name.contains(c)) continue;
        QMessageBox::warning(this, u"Invalid name"_s,
                             u"A name cannot contain: %1"_s.arg(forbidden));
        return;
    }
    if (name.startsWith(u'.')) {
        QMessageBox::warning(this, u"Invalid name"_s, u"A name cannot start with a dot."_s);
        return;
    }

    const QString dir = m_collectionsRoot + u"/"_s + name;
    if (QDir(dir).exists()) {
        QMessageBox::warning(this, u"Already exists"_s,
                             u"A collection with that name already exists."_s);
        return;
    }
    if (!QDir().mkpath(dir)) {
        QMessageBox::warning(this, u"Could not create"_s, u"Failed to create %1"_s.arg(dir));
        return;
    }

    refreshCollections();
    if (const int at = m_collections->findText(name); at >= 0) m_collections->setCurrentIndex(at);
    persistSettings();
}

QString CollectorPage::currentCollectionDir() const
{
    const QString name = m_collections->currentText();
    return name.isEmpty() ? QString() : m_collectionsRoot + u"/"_s + name;
}

void CollectorPage::startOrStop()
{
    if (m_watcher->isRunning()) {
        m_watcher->stop();
        return;
    }

    const QString watch = m_watchEdit->text().trimmed();
    if (watch.isEmpty() || !QDir(watch).exists()) {
        QMessageBox::warning(this, u"Watch folder"_s,
                             u"Set a valid watch folder before starting."_s);
        return;
    }
    if (m_collections->currentText().isEmpty()) {
        QMessageBox::warning(this, u"Collection"_s,
                             u"Pick or create a collection before starting."_s);
        return;
    }

    persistSettings();
    m_watcher->start(watch, currentCollectionDir(), m_threshold->value(), m_poll->value());
}

void CollectorPage::setRunningUi(bool running)
{
    m_startStop->setText(running ? u"Stop"_s : u"Start"_s);
    m_runState->setText(running ? u"running"_s : u"idle"_s);

    // Locked while it runs: these are what the watcher was started with, and
    // changing one mid-run would move files somewhere the log does not say.
    m_watchEdit->setEnabled(!running);
    m_watchBrowse->setEnabled(!running);
    m_collections->setEnabled(!running);
    m_newCollection->setEnabled(!running);
    m_poll->setEnabled(!running);
    m_rebuild->setEnabled(!running);
}

void CollectorPage::rebuildIndex()
{
    const QString dir = currentCollectionDir();
    if (dir.isEmpty()) return;

    if (QMessageBox::question(this, u"Rebuild index"_s,
                              u"Re-hash every image in this collection and write "
                              u"__hashes.json again?\n\nThe existing hashes are dropped first."_s)
        != QMessageBox::Yes)
        return;

    m_rebuild->setEnabled(false);
    m_log->appendPlainText(u"[%1] rebuilding index..."_s.arg(timestamp()));

    // Hashing a large collection takes a while, and doing it on the GUI
    // thread would freeze the window for all of it.
    auto* watcher = new QFutureWatcher<qsizetype>(this);
    connect(watcher, &QFutureWatcher<qsizetype>::finished, this, [this, watcher]() {
        m_log->appendPlainText(
            u"[%1] rebuild done - %2 entries"_s.arg(timestamp()).arg(watcher->result()));
        m_rebuild->setEnabled(true);
        watcher->deleteLater();
    });

    watcher->setFuture(QtConcurrent::run([dir]() -> qsizetype {
        PHashIndex index; // from scratch, not a merge
        QDirIterator it(dir, kImageFilters, QDir::Files);
        while (it.hasNext()) {
            const QString path = it.next();
            if (const uint64_t hash = phashFile(path); hash != 0)
                index.add(QFileInfo(path).fileName(), hash);
        }
        index.save(dir);
        return index.size();
    }));
}

void CollectorPage::persistSettings()
{
    m_settings->collectorWatchFolder = m_watchEdit->text().trimmed();
    m_settings->collectorActiveCollection = m_collections->currentText();
    m_settings->collectorThreshold = m_threshold->value();
    m_settings->collectorPollSeconds = m_poll->value();
}

void CollectorPage::addRecentThumb(const QString& imagePath)
{
    // One watcher per call: several images can land in a single poll, and a
    // shared watcher would drop all but the last.
    auto* watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, imagePath]() {
        const QImage image = watcher->result();
        watcher->deleteLater();
        if (image.isNull()) return;

        // Expanded to fill the square and then centre-cropped, so a row of
        // mixed aspect ratios still reads as a grid.
        QPixmap thumb = QPixmap::fromImage(image).scaled(
            kThumbSize, kThumbSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        if (thumb.width() > kThumbSize || thumb.height() > kThumbSize)
            thumb = thumb.copy((thumb.width() - kThumbSize) / 2,
                               (thumb.height() - kThumbSize) / 2, kThumbSize, kThumbSize);

        auto* item = new QListWidgetItem;
        item->setIcon(QIcon(thumb));
        item->setText(QFileInfo(imagePath).fileName());
        item->setData(Qt::UserRole, imagePath);
        item->setToolTip(imagePath);
        m_recent->insertItem(0, item);

        while (m_recent->count() > kRecentMax)
            delete m_recent->takeItem(m_recent->count() - 1);

        if (!m_recentEmpty->isVisible()) return;
        m_recentEmpty->setVisible(false);
        m_recent->setVisible(true);
    });

    watcher->setFuture(QtConcurrent::run([imagePath]() { return QImage(imagePath); }));
}

} // namespace tc
