#include <gui/composer/previewpopoutwindow.h>
#include <gui/composer/scaledimagelabel.h>
#include <gui/composer/clickablelabel.h>
#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QImage>
#include <QKeySequence>
#include <QPixmap>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace gui {

PreviewPopoutWindow::PreviewPopoutWindow(QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint)
{
    setObjectName("PreviewPopout");
    setWindowTitle("Preview");
    resize(900, 700);
    setMinimumSize(400, 300);
    setAttribute(Qt::WA_StyledBackground);

    m_imageLabel = new ScaledImageLabel(this);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(m_imageLabel);

    // Floating small label: bottom-left, shows newest temp-folder image
    m_tempLabel = new ClickableLabel(this);
    m_tempLabel->setObjectName("PopoutTempLabel");
    m_tempLabel->hide();

    m_watcher     = new QFileSystemWatcher(this);
    m_debounce    = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(300);
    m_loadWatcher = new QFutureWatcher<QImage>(this);

    connect(m_watcher,  &QFileSystemWatcher::directoryChanged,
            this, [this](const QString&) { m_debounce->start(); });
    connect(m_debounce, &QTimer::timeout,
            this, [this]() { loadNewestTempImage(); });
    connect(m_loadWatcher, &QFutureWatcher<QImage>::finished, this, [this]() {
        const QImage img = m_loadWatcher->result();
        if (img.isNull()) return;
        m_tempLabel->setSourcePixmap(QPixmap::fromImage(img));
        m_tempLabel->setFilePath(m_lastTempPath);
        m_tempLabel->show();
        m_tempLabel->raise();
    });

    // TODO: fix, should be F11 but currently results in ambiguous shortcut call conflict.
    auto* action = new QAction(this);
    action->setShortcut(QKeySequence("F12"));
    action->setShortcutContext(Qt::WindowShortcut);
    connect(action, &QAction::triggered, this, [this]() {
        m_isFullScreen = !m_isFullScreen;
        if (m_isFullScreen)
            showFullScreen();
        else
            showNormal();
        });
    addAction(action);
}

void PreviewPopoutWindow::setImage(const QPixmap& pix)
{
    m_imageLabel->setSourcePixmap(pix);
}

void PreviewPopoutWindow::setTempFolder(const QString& folder)
{
    if (m_tempFolder == folder) return;
    if (!m_watcher->directories().isEmpty())
        m_watcher->removePaths(m_watcher->directories());
    m_tempFolder = folder;
    if (!folder.isEmpty() && QDir(folder).exists())
        m_watcher->addPath(folder);
}

void PreviewPopoutWindow::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    constexpr int margin = 12;
    const int side = qBound(120, qMin(width(), height()) / 2, 800);
    m_tempLabel->setFixedSize(side, side);
    m_tempLabel->move(margin, height() - side - margin);
}

void PreviewPopoutWindow::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    loadNewestTempImage();
}

void PreviewPopoutWindow::loadNewestTempImage()
{
    if (m_tempFolder.isEmpty()) return;
    static const QStringList filters = {"*.png", "*.jpg", "*.jpeg", "*.webp"};
    const QFileInfoList files = QDir(m_tempFolder).entryInfoList(filters, QDir::Files);
    if (files.isEmpty()) return;

    const QFileInfo* newest = &files[0];
    for (const QFileInfo& fi : files)
        if (fi.lastModified() > newest->lastModified()) newest = &fi;

    if (m_loadWatcher->isRunning()) return;
    m_lastTempPath = newest->absoluteFilePath();
    m_loadWatcher->setFuture(QtConcurrent::run([path = m_lastTempPath]() -> QImage {
        return QImage(path);
    }));
}

} // namespace gui
