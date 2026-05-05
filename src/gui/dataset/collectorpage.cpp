#include <gui/dataset/collectorpage.h>
#include <gui/widgets/appscrollbar.h>
#include <gui/widgets/composericons.h>
#include <core/downloadwatcher.h>
#include <core/phasher.h>
#include <core/phashindex.h>
#include <utils/appconfig.h>
#include <utils/appsettings.h>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSlider>
#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QListView>
#include <QImage>
#include <QPixmap>
#include <QIcon>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QDir>
#include <QDirIterator>
#include <QDesktopServices>
#include <QUrl>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QApplication>
#include <QDateTime>

using namespace utils;

namespace gui {

namespace {
constexpr int kPanelWidth = 380;
constexpr int kFolderRowHeight = 40;
constexpr int kRecentPanelWidth = 420; // matches AutoTagPage's preview column
constexpr int kRecentThumb = 96;       // square thumb side, in px
constexpr int kRecentMax = 50;         // newest-first cap; oldest rolls off

QWidget* makeSectionHeader(QWidget* parent, const QString& title)
{
    auto* h = new QWidget(parent);
    h->setObjectName("DatasetSectionHeader");
    h->setAttribute(Qt::WA_StyledBackground, true);
    h->setFixedHeight(50);
    auto* l = new QHBoxLayout(h);
    l->setContentsMargins(16, 12, 16, 12);
    l->setSpacing(8);
    auto* lbl = new QLabel(title, h);
    lbl->setObjectName("DatasetSectionTitle");
    l->addWidget(lbl);
    l->addStretch();
    return h;
}

QString timestamp()
{
    return QDateTime::currentDateTime().toString("HH:mm:ss");
}
} // namespace

CollectorPage::CollectorPage(utils::AppSettings* settings, QWidget* parent)
    : QWidget(parent), m_settings(settings)
{
    setObjectName("CollectorPage");
    setAttribute(Qt::WA_StyledBackground, true);

    m_watcher = new core::DownloadWatcher(this);

    // ---- Left panel
    auto* leftPanel = new QWidget(this);
    leftPanel->setObjectName("DatasetParamsPanel");
    leftPanel->setAttribute(Qt::WA_StyledBackground, true);
    leftPanel->setFixedWidth(kPanelWidth);

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

    // Watch folder
    m_watchEdit = new QLineEdit(leftBody);
    m_watchEdit->setObjectName("SearchBar");
    m_watchEdit->setPlaceholderText("Folder to watch (typically Downloads)");
    m_watchEdit->setFixedHeight(kFolderRowHeight);
    m_watchEdit->setToolTip("The Watcher polls this folder for new image files. Each one is\n"
                            "hashed and either moved into the collection (renamed sequentially)\n"
                            "or sent to the recycle bin if it duplicates an existing entry.");
    m_watchBrowseBtn = new QPushButton("…", leftBody);
    m_watchBrowseBtn->setObjectName("DatasetBrowseBtn");
    m_watchBrowseBtn->setFixedSize(36, kFolderRowHeight);
    m_watchBrowseBtn->setToolTip("Pick the watch folder.");

    auto* watchRow = new QHBoxLayout;
    watchRow->setContentsMargins(0, 0, 0, 0);
    watchRow->setSpacing(4);
    watchRow->addWidget(m_watchEdit, 1);
    watchRow->addWidget(m_watchBrowseBtn);

    // Collection picker
    m_collectionBox = new QComboBox(leftBody);
    m_collectionBox->setObjectName("DatasetSpin");
    m_collectionBox->setFixedHeight(kFolderRowHeight);
    m_collectionBox->setToolTip(
        "Active collection - new images land in data/collections/<name>/\n"
        "renamed as 00001.png, 00002.jpg, … Each collection has its own\n"
        "__hashes.json so duplicate detection picks up where you left off.");

    m_newCollectionBtn = new QPushButton("New", leftBody);
    m_newCollectionBtn->setObjectName("DatasetBrowseBtn");
    m_newCollectionBtn->setFixedSize(56, kFolderRowHeight);
    m_newCollectionBtn->setToolTip(
        "Create a new collection folder. You'll be prompted for a name -\n"
        "use anything filesystem-safe (no slashes, no leading dots).");

    auto* collectionRow = new QHBoxLayout;
    collectionRow->setContentsMargins(0, 0, 0, 0);
    collectionRow->setSpacing(4);
    collectionRow->addWidget(m_collectionBox, 1);
    collectionRow->addWidget(m_newCollectionBtn);

    // Threshold
    m_thresholdSlider = new QSlider(Qt::Horizontal, leftBody);
    m_thresholdSlider->setObjectName("DatasetPmiSlider");
    m_thresholdSlider->setRange(0, 16);
    m_thresholdSlider->setValue(4);
    m_thresholdSlider->setToolTip(
        "Hamming distance cutoff for the duplicate check. A new image is\n"
        "skipped (sent to the recycle bin) if it's within this many bits\n"
        "of any existing entry.\n"
        "  0–2  : nearly identical (recompresses, watermark removed, etc)\n"
        "  3–6  : same image, different edits - sweet spot for dedup\n"
        "  7+   : looser, may false-merge similar but distinct images");

    m_thresholdValue = new QLabel(leftBody);
    m_thresholdValue->setObjectName("DatasetParamValue");
    m_thresholdValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_thresholdValue->setMinimumWidth(28);

    auto syncThreshold = [this]() {
        m_thresholdValue->setText(QString::number(m_thresholdSlider->value()));
    };
    syncThreshold();

    auto* thresholdRow = new QHBoxLayout;
    thresholdRow->setContentsMargins(0, 0, 0, 0);
    thresholdRow->setSpacing(6);
    thresholdRow->addWidget(m_thresholdSlider, 1);
    thresholdRow->addWidget(m_thresholdValue);

    // Poll interval
    m_pollSpin = new QSpinBox(leftBody);
    m_pollSpin->setObjectName("DatasetSpin");
    m_pollSpin->setRange(1, 60);
    m_pollSpin->setValue(5);
    m_pollSpin->setSuffix(" s");
    m_pollSpin->setToolTip("How often the watcher scans the source folder. Shorter = more\n"
                           "responsive but slightly more CPU. 5 seconds is fine for typical use.");

    // Start/Stop + Open + Rebuild
    m_startStopBtn = new QPushButton("Start", leftBody);
    m_startStopBtn->setObjectName("DatasetRunBtn");
    m_startStopBtn->setToolTip("Begin watching. The button becomes Stop while the watcher is\n"
                               "active; the app shuts the watcher down automatically on exit.");

    m_openFolderBtn = new QPushButton("Open collection", leftBody);
    m_openFolderBtn->setObjectName("DatasetBrowseBtn");
    m_openFolderBtn->setFixedHeight(kFolderRowHeight);
    m_openFolderBtn->setToolTip("Reveal the active collection folder in your file manager.");

    m_rebuildBtn = new QPushButton("Rebuild index", leftBody);
    m_rebuildBtn->setObjectName("DatasetBrowseBtn");
    m_rebuildBtn->setFixedHeight(kFolderRowHeight);
    m_rebuildBtn->setToolTip("Walk the collection folder and recompute every image's pHash from\n"
                             "scratch, overwriting __hashes.json. Use this if the index was\n"
                             "deleted, the folder was edited externally, or duplicate detection\n"
                             "is missing existing files.");

    m_sendToTaggerBtn = new QPushButton("Send to Auto-tagger", leftBody);
    m_sendToTaggerBtn->setObjectName("EntryActionBtn");
    m_sendToTaggerBtn->setToolTip("Open this collection's folder in the Auto-tagger tab as the\n"
                                  "input folder, ready to run inference over.");

    auto* grid = new QGridLayout;
    grid->setSpacing(6);
    grid->setColumnStretch(1, 1);
    int r = 0;
    grid->addWidget(mkLabel("Threshold"), r, 0);
    grid->addLayout(thresholdRow, r++, 1);
    grid->addWidget(mkLabel("Poll"), r, 0);
    grid->addWidget(m_pollSpin, r++, 1);

    {
        auto* row = new QWidget(leftBody);
        auto* l = new QHBoxLayout(row);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(4);
        auto* lblw = new QLabel("Watch folder", row);
        lblw->setObjectName("DatasetParamLabel");
        auto* openBtn = new QPushButton(row);
        openBtn->setObjectName("SidebarBtn");
        openBtn->setFixedSize(20, 20);
        openBtn->setIcon(gui::icons::openExternal());
        openBtn->setIconSize(QSize(14, 14));
        openBtn->setCursor(Qt::PointingHandCursor);
        openBtn->setToolTip("Open this folder in the system file manager.");
        connect(openBtn, &QPushButton::clicked, this, [this]() {
            const QString d = m_watchEdit->text().trimmed();
            if (d.isEmpty() || !QDir(d).exists()) return;
            QDesktopServices::openUrl(QUrl::fromLocalFile(d));
        });
        l->addWidget(lblw, 1);
        l->addWidget(openBtn);
        lbl->addWidget(row);
    }
    lbl->addLayout(watchRow);
    lbl->addSpacing(4);
    lbl->addWidget(mkLabel("Collection"));
    lbl->addLayout(collectionRow);
    lbl->addSpacing(8);
    lbl->addLayout(grid);
    lbl->addSpacing(8);
    lbl->addWidget(m_startStopBtn);
    lbl->addWidget(m_openFolderBtn);
    lbl->addWidget(m_rebuildBtn);
    lbl->addWidget(m_sendToTaggerBtn);
    lbl->addStretch();

    ll->addWidget(makeSectionHeader(leftPanel, "AUTO-COLLECT"));
    ll->addWidget(leftBody, 1);

    // ---- Right panel
    auto* rightPanel = new QWidget(this);
    auto* rl = new QVBoxLayout(rightPanel);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);

    auto* rightBody = new QWidget(rightPanel);
    auto* rbl = new QVBoxLayout(rightBody);
    rbl->setContentsMargins(12, 12, 12, 12);
    rbl->setSpacing(8);

    // Active dot + counters
    m_activeDot = new QLabel(rightBody);
    m_activeDot->setObjectName("DatasetStatusLabel");
    m_activeDot->setAlignment(Qt::AlignCenter);
    m_activeDot->setText("● idle");

    m_collectedLbl = new QLabel(rightBody);
    m_collectedLbl->setObjectName("DatasetParamValue");
    m_skippedLbl = new QLabel(rightBody);
    m_skippedLbl->setObjectName("DatasetParamValue");

    auto* statRow = new QHBoxLayout;
    statRow->setContentsMargins(0, 0, 0, 0);
    statRow->setSpacing(16);
    statRow->addWidget(m_activeDot);
    statRow->addStretch();
    statRow->addWidget(m_collectedLbl);
    statRow->addWidget(m_skippedLbl);

    m_log = new QPlainTextEdit(rightBody);
    m_log->setObjectName("DatasetExcludeEdit");
    m_log->setReadOnly(true);
    m_log->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));
    m_log->setPlaceholderText("Activity log will appear here once the watcher is running.");

    rbl->addLayout(statRow);
    rbl->addWidget(m_log, 1);

    rl->addWidget(makeSectionHeader(rightPanel, "ACTIVITY"));
    rl->addWidget(rightBody, 1);

    auto syncCounters = [this]() {
        m_collectedLbl->setText(QString("Collected: %1").arg(m_watcher->collectedCount()));
        m_skippedLbl->setText(QString("Skipped: %1").arg(m_watcher->skippedCount()));
    };
    syncCounters();

    // ---- Far-right panel (recent thumbs)
    // Newest-first grid; column position/width match AutoTagPage's preview.
    auto* recentPanel = new QWidget(this);
    recentPanel->setObjectName("DatasetPreviewPanel");
    recentPanel->setAttribute(Qt::WA_StyledBackground, true);
    recentPanel->setFixedWidth(kRecentPanelWidth);

    auto* rcl = new QVBoxLayout(recentPanel);
    rcl->setContentsMargins(0, 0, 0, 0);
    rcl->setSpacing(0);

    auto* recentBody = new QWidget(recentPanel);
    auto* rcbl = new QVBoxLayout(recentBody);
    rcbl->setContentsMargins(12, 12, 12, 12);
    rcbl->setSpacing(8);

    m_recentList = new QListWidget(recentBody);
    m_recentList->setObjectName("DatasetResultsList");
    m_recentList->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));
    m_recentList->setFrameShape(QFrame::NoFrame);
    m_recentList->setViewMode(QListView::IconMode);
    m_recentList->setIconSize(QSize(kRecentThumb, kRecentThumb));
    m_recentList->setResizeMode(QListView::Adjust);
    m_recentList->setMovement(QListView::Static);
    m_recentList->setSpacing(6);
    m_recentList->setUniformItemSizes(true);
    m_recentList->setWordWrap(true);
    m_recentList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_recentList->setVisible(false); // hidden until first thumb arrives

    m_recentEmpty = new QLabel("Recently collected images will appear here.", recentBody);
    m_recentEmpty->setObjectName("DatasetEmptyState");
    m_recentEmpty->setAlignment(Qt::AlignCenter);
    m_recentEmpty->setWordWrap(true);

    rcbl->addWidget(m_recentList, 1);
    rcbl->addWidget(m_recentEmpty, 1);

    rcl->addWidget(makeSectionHeader(recentPanel, "RECENT"));
    rcl->addWidget(recentBody, 1);

    // Click / Enter on a recent thumb opens it in the system viewer.
    auto openRecent = [](QListWidgetItem* it) {
        if (!it) return;
        const QString path = it->data(Qt::UserRole).toString();
        if (!path.isEmpty() && QFile::exists(path))
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    };
    connect(m_recentList, &QListWidget::itemActivated, this, openRecent);
    connect(m_recentList, &QListWidget::itemDoubleClicked, this, openRecent);

    // ---- Root
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(leftPanel);
    root->addWidget(rightPanel, 1);
    root->addWidget(recentPanel);

    // ---- Hydrate from settings
    if (m_settings) {
        if (!m_settings->collectorWatchFolder.isEmpty())
            m_watchEdit->setText(m_settings->collectorWatchFolder);
        m_thresholdSlider->setValue(qBound(0, m_settings->collectorThreshold, 16));
        m_pollSpin->setValue(qBound(1, m_settings->collectorPollSeconds, 60));
    }
    syncThreshold();
    refreshCollections();
    if (m_settings) {
        const int idx = m_collectionBox->findText(m_settings->collectorActiveCollection);
        if (idx >= 0) m_collectionBox->setCurrentIndex(idx);
    }

    // ---- Wire
    connect(m_watchBrowseBtn, &QPushButton::clicked, this, [this]() {
        const QString d =
            QFileDialog::getExistingDirectory(this, "Choose watch folder", m_watchEdit->text());
        if (!d.isEmpty()) {
            m_watchEdit->setText(d);
            persistSettings();
        }
    });
    connect(m_watchEdit, &QLineEdit::editingFinished, this, [this]() { persistSettings(); });

    connect(m_collectionBox, &QComboBox::currentTextChanged, this,
            [this](const QString&) { persistSettings(); });
    connect(m_newCollectionBtn, &QPushButton::clicked, this, &CollectorPage::onNewCollection);

    connect(m_thresholdSlider, &QSlider::valueChanged, this, [this, syncThreshold](int) {
        syncThreshold();
        persistSettings();
    });
    connect(m_pollSpin, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int) { persistSettings(); });

    connect(m_startStopBtn, &QPushButton::clicked, this, &CollectorPage::onStartStop);
    connect(m_openFolderBtn, &QPushButton::clicked, this, [this]() {
        const QString d = currentCollectionDir();
        if (d.isEmpty()) return;
        QDir().mkpath(d);
        QDesktopServices::openUrl(QUrl::fromLocalFile(d));
    });
    connect(m_rebuildBtn, &QPushButton::clicked, this, &CollectorPage::onRebuildIndex);

    connect(m_sendToTaggerBtn, &QPushButton::clicked, this, [this]() {
        const QString d = currentCollectionDir();
        if (d.isEmpty()) return;
        emit sendToAutoTaggerRequested(d);
    });

    // Disable "Send to Auto-tagger" / "Open collection" while no collection
    // is picked. Tracks the dropdown live; refresh after construction below.
    auto syncCollectionButtons = [this]() {
        const bool ok = !m_collectionBox->currentText().isEmpty();
        m_sendToTaggerBtn->setEnabled(ok);
        m_openFolderBtn->setEnabled(ok);
        m_rebuildBtn->setEnabled(ok && !m_watcher->isRunning());
    };
    syncCollectionButtons();
    connect(m_collectionBox, &QComboBox::currentTextChanged, this,
            [syncCollectionButtons](const QString&) { syncCollectionButtons(); });

    // Watcher signals
    connect(m_watcher, &core::DownloadWatcher::imageMoved, this,
            [this](QString fromAbs, QString toAbs) {
                const QString fromName = QFileInfo(fromAbs).fileName();
                const QString toName = QFileInfo(toAbs).fileName();
                m_log->appendPlainText(
                    QString("[%1] moved %2 → %3").arg(timestamp(), fromName, toName));
                addRecentThumb(toAbs);
            });
    connect(m_watcher, &core::DownloadWatcher::imageSkipped, this,
            [this](QString fromAbs, int dist, QString matchName) {
                const QString fromName = QFileInfo(fromAbs).fileName();
                m_log->appendPlainText(QString("[%1] dup %2 (dist=%3, matches %4) → recycle bin")
                                           .arg(timestamp(), fromName)
                                           .arg(dist)
                                           .arg(matchName));
            });
    connect(m_watcher, &core::DownloadWatcher::error, this, [this](QString msg) {
        m_log->appendPlainText(QString("[%1] error - %2").arg(timestamp(), msg));
    });
    connect(m_watcher, &core::DownloadWatcher::status, this,
            [this, syncCounters](int, int) { syncCounters(); });
    connect(m_watcher, &core::DownloadWatcher::started, this, [this]() {
        m_log->appendPlainText(QString("[%1] started").arg(timestamp()));
        setRunningUi(true);
    });
    connect(m_watcher, &core::DownloadWatcher::stopped, this, [this]() {
        m_log->appendPlainText(QString("[%1] stopped").arg(timestamp()));
        setRunningUi(false);
    });
}

CollectorPage::~CollectorPage()
{
    stopWatcher();
}

void CollectorPage::stopWatcher()
{
    if (m_watcher) m_watcher->stop();
}

// ---- Collections

void CollectorPage::refreshCollections()
{
    const QString root = BASE_PATH + "/" + COLLECTIONS_DIR;
    QDir().mkpath(root);

    const QString prev = m_collectionBox->currentText();
    QSignalBlocker block(m_collectionBox);
    m_collectionBox->clear();

    const QFileInfoList subs =
        QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& fi : subs)
        m_collectionBox->addItem(fi.fileName());

    if (!prev.isEmpty()) {
        const int idx = m_collectionBox->findText(prev);
        if (idx >= 0) m_collectionBox->setCurrentIndex(idx);
    }
}

void CollectorPage::onNewCollection()
{
    bool ok = false;
    QString name = QInputDialog::getText(this, "New collection",
                                         "Collection name:", QLineEdit::Normal, QString(), &ok);
    if (!ok) return;

    name = name.trimmed();
    if (name.isEmpty()) return;

    // Block path-traversal-y characters. Anything else the OS rejects will
    // surface naturally on mkpath failure.
    static const QString forbidden = "\\/:*?\"<>|";
    for (QChar c : forbidden) {
        if (name.contains(c)) {
            QMessageBox::warning(this, "Invalid name",
                                 QString("Name can't contain: %1").arg(forbidden));
            return;
        }
    }
    if (name.startsWith('.')) {
        QMessageBox::warning(this, "Invalid name", "Name can't start with a dot.");
        return;
    }

    const QString dir = BASE_PATH + "/" + COLLECTIONS_DIR + "/" + name;
    if (QDir(dir).exists()) {
        QMessageBox::warning(this, "Already exists", "A collection with that name already exists.");
        return;
    }
    if (!QDir().mkpath(dir)) {
        QMessageBox::warning(this, "Couldn't create", QString("Failed to create %1").arg(dir));
        return;
    }

    refreshCollections();
    const int idx = m_collectionBox->findText(name);
    if (idx >= 0) m_collectionBox->setCurrentIndex(idx);
    persistSettings();
}

QString CollectorPage::currentCollectionDir() const
{
    const QString name = m_collectionBox->currentText();
    if (name.isEmpty()) return {};
    return BASE_PATH + "/" + COLLECTIONS_DIR + "/" + name;
}

// ---- Start / stop

void CollectorPage::onStartStop()
{
    if (m_watcher->isRunning()) {
        m_watcher->stop();
        return;
    }

    const QString watch = m_watchEdit->text().trimmed();
    if (watch.isEmpty() || !QDir(watch).exists()) {
        QMessageBox::warning(this, "Watch folder", "Set a valid watch folder before starting.");
        return;
    }
    if (m_collectionBox->currentText().isEmpty()) {
        QMessageBox::warning(this, "Collection", "Pick or create a collection before starting.");
        return;
    }

    persistSettings();
    m_watcher->start(watch, currentCollectionDir(), m_thresholdSlider->value(),
                     m_pollSpin->value());
}

void CollectorPage::setRunningUi(bool running)
{
    m_startStopBtn->setText(running ? "Stop" : "Start");
    m_activeDot->setText(running ? "● running" : "● idle");
    // Lock controls that would change the watcher's contract mid-run.
    m_watchEdit->setEnabled(!running);
    m_watchBrowseBtn->setEnabled(!running);
    m_collectionBox->setEnabled(!running);
    m_newCollectionBtn->setEnabled(!running);
    m_pollSpin->setEnabled(!running);
    m_rebuildBtn->setEnabled(!running);
}

// ---- Rebuild index

void CollectorPage::onRebuildIndex()
{
    const QString dir = currentCollectionDir();
    if (dir.isEmpty()) return;

    if (QMessageBox::question(this, "Rebuild index",
                              "Walk every image in this collection and recompute __hashes.json "
                              "from scratch?\n\nExisting hashes are dropped first.") !=
        QMessageBox::Yes) {
        return;
    }

    m_rebuildBtn->setEnabled(false);
    m_log->appendPlainText(QString("[%1] rebuilding index…").arg(timestamp()));

    // Hashing N files can take a moment for large collections; do it on a
    // worker thread so the UI doesn't freeze.
    auto* watcher = new QFutureWatcher<int>(this);
    connect(watcher, &QFutureWatcher<int>::finished, this, [this, watcher, dir]() {
        const int n = watcher->result();
        m_log->appendPlainText(QString("[%1] rebuild done - %2 entries").arg(timestamp()).arg(n));
        m_rebuildBtn->setEnabled(true);
        watcher->deleteLater();
    });

    watcher->setFuture(QtConcurrent::run([dir]() -> int {
        core::PHashIndex idx; // start fresh
        const QStringList exts = {"*.png", "*.jpg", "*.jpeg", "*.webp", "*.bmp", "*.gif"};
        QDirIterator it(dir, exts, QDir::Files);
        while (it.hasNext()) {
            const QString abs = it.next();
            const uint64_t h = core::phashFile(abs);
            if (h == 0) continue;
            idx.add(QFileInfo(abs).fileName(), h);
        }
        idx.saveToDir(dir);
        return idx.size();
    }));
}

void CollectorPage::persistSettings()
{
    if (!m_settings) return;
    m_settings->collectorWatchFolder = m_watchEdit->text().trimmed();
    m_settings->collectorActiveCollection = m_collectionBox->currentText();
    m_settings->collectorThreshold = m_thresholdSlider->value();
    m_settings->collectorPollSeconds = m_pollSpin->value();
}

// ---- Recent thumbs

void CollectorPage::addRecentThumb(const QString& imagePath)
{
    if (!m_recentList) return;

    // Decode off the GUI thread; per-call watcher so multiple in-flight
    // loads don't step on each other.
    auto* w = new QFutureWatcher<QImage>(this);
    connect(w, &QFutureWatcher<QImage>::finished, this, [this, w, imagePath]() {
        const QImage img = w->result();
        w->deleteLater();
        if (img.isNull()) return;

        // KeepAspectRatioByExpanding fills the icon square, then we crop
        // the centre via QPixmap::copy so the grid stays visually uniform.
        QPixmap pm = QPixmap::fromImage(img).scaled(
            kRecentThumb, kRecentThumb, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        if (pm.width() > kRecentThumb || pm.height() > kRecentThumb) {
            const int x = (pm.width() - kRecentThumb) / 2;
            const int y = (pm.height() - kRecentThumb) / 2;
            pm = pm.copy(x, y, kRecentThumb, kRecentThumb);
        }

        auto* it = new QListWidgetItem;
        it->setIcon(QIcon(pm));
        it->setText(QFileInfo(imagePath).fileName());
        it->setData(Qt::UserRole, imagePath);
        it->setToolTip(imagePath);
        m_recentList->insertItem(0, it);

        // Cap at kRecentMax - drop oldest from the bottom.
        while (m_recentList->count() > kRecentMax)
            delete m_recentList->takeItem(m_recentList->count() - 1);

        // First thumb arrived: swap the empty-state placeholder for the grid.
        if (m_recentEmpty && m_recentEmpty->isVisible()) {
            m_recentEmpty->setVisible(false);
            m_recentList->setVisible(true);
        }
    });
    w->setFuture(QtConcurrent::run([imagePath]() -> QImage { return QImage(imagePath); }));
}

} // namespace gui
