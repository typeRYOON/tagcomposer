#include <app/preview_popout_window.h>
#include <app/frameless_chrome.h>
#include <app/movable_preview_label.h>
#include <app/status_bar.h>
#include <app/title_bar.h>
#include <app/widget_utils.h>
#include <app/window_chrome.h>
#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QImage>
#include <QKeyEvent>
#include <QKeySequence>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindowStateChangeEvent>
#include <QtConcurrent>

using namespace Qt::StringLiterals;

namespace tc {

ScaledImageLabel::ScaledImageLabel(QWidget* parent) : QLabel(parent)
{
    setObjectName(u"PopoutImageLabel"_s);
    setAlignment(Qt::AlignCenter);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(50, 50);
    setAttribute(Qt::WA_StyledBackground);
}

void ScaledImageLabel::setSourcePixmap(const QPixmap& pixmap)
{
    m_source = pixmap;
    updateScaled();
}

void ScaledImageLabel::resizeEvent(QResizeEvent* event)
{
    QLabel::resizeEvent(event);
    updateScaled();
}

void ScaledImageLabel::updateScaled()
{
    if (m_source.isNull() || width() <= 0 || height() <= 0) return;
    setPixmap(m_source.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

PreviewPopoutWindow::PreviewPopoutWindow(QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinimizeButtonHint
                          | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint)
{
    setObjectName(u"PreviewPopout"_s);
    setWindowTitle(u"Preview"_s);
    resize(900, 700);
    setMinimumSize(400, 300);
    setAttribute(Qt::WA_StyledBackground);
    setWindowOpacity(0.0); // faded in by the first showEvent

    m_imageLabel = new ScaledImageLabel(this);

    // Non-modal, so no explicit mouse grab is needed.
    m_chrome = new WindowChrome(this);
    m_statusBar = new StatusBar(this);

    auto* contentLayout = new QVBoxLayout(m_chrome->body());
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(m_imageLabel, 1);
    contentLayout->addWidget(m_statusBar);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(m_chrome->frame());

    m_tempLabel = new MovablePreviewLabel(this);
    m_tempLabel->setObjectName(u"PopoutTempLabel"_s);
    m_tempLabel->setMinimumSize(80, 80);
    m_tempLabel->hide();

    m_watcher = new QFileSystemWatcher(this);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(300);

    m_loadWatcher = new QFutureWatcher<QImage>(this);

    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString&) { m_debounce->start(); });
    connect(m_debounce, &QTimer::timeout, this, [this]() { loadNewestTempImage(); });
    connect(m_loadWatcher, &QFutureWatcher<QImage>::finished, this, [this]() {
        const QImage image = m_loadWatcher->result();
        if (!image.isNull()) {
            m_tempLabel->setSourcePixmap(QPixmap::fromImage(image));
            m_tempLabel->setFilePath(m_lastTempPath);
            m_tempLabel->show();
            m_tempLabel->raise();
        }
        // Re-check in case the folder changed during the load.
        m_debounce->start();
    });

    // Window-scoped, so they don't clash with the main window's bindings.
    auto addShortcut = [this](const QString& sequence, auto handler) {
        auto* action = new QAction(this);
        action->setShortcut(QKeySequence(sequence));
        action->setShortcutContext(Qt::WindowShortcut);
        connect(action, &QAction::triggered, this, handler);
        addAction(action);
    };

    addShortcut(u"F11"_s, [this]() {
        QPropertyAnimation* out = propertyAnimate(this, "windowOpacity", windowOpacity(), 0.0,
                                                  200, QEasingCurve::InOutSine);
        connect(out, &QPropertyAnimation::finished, this, [this]() {
            if (isFullScreen())
                showNormal();
            else
                showFullScreen();
            propertyAnimate(this, "windowOpacity", windowOpacity(), 1.0, 200,
                            QEasingCurve::InOutSine);
        });
    });
    addShortcut(u"Shift+E"_s, [this]() { emit runRequested(); });
    addShortcut(u"Shift+R"_s, [this]() { emit interruptRequested(); });
    addShortcut(u"Shift+Alt+R"_s, [this]() { emit clearPendingRequested(); });
    addShortcut(u"Ctrl+W"_s, [this]() { close(); });
    addShortcut(u"Ctrl+H"_s, [this]() {
        if (windowState() & Qt::WindowMinimized) return;
        QPropertyAnimation* out = propertyAnimate(this, "windowOpacity", windowOpacity(), 0.0,
                                                  200, QEasingCurve::InOutSine);
        connect(out, &QPropertyAnimation::finished, this, [this]() { showMinimized(); });
    });
}

void PreviewPopoutWindow::setImage(const QPixmap& image)
{
    m_imageLabel->setSourcePixmap(image);
}

void PreviewPopoutWindow::setOutputFolder(const QString& folder)
{
    if (m_tempLabel) m_tempLabel->setOutputFolder(folder);
}

void PreviewPopoutWindow::setProgress(int step, int total)
{
    if (m_statusBar) m_statusBar->setProgress(step, total);
}

void PreviewPopoutWindow::setActiveCount(int count)
{
    if (m_statusBar) m_statusBar->setActiveCount(count);
}

void PreviewPopoutWindow::setTempFolder(const QString& folder)
{
    if (m_tempLabel) m_tempLabel->setTempFolder(folder);
    if (m_tempFolder == folder) return;

    if (!m_watcher->directories().isEmpty()) m_watcher->removePaths(m_watcher->directories());
    m_tempFolder = folder;
    if (!folder.isEmpty() && QDir(folder).exists()) m_watcher->addPath(folder);
}

void PreviewPopoutWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    constexpr int margin = 12;

    // Keep the inset between the titlebar and the status bar, with a margin.
    constexpr int inset = chrome::kResizeBorder + margin;
    QRect bounds(inset, inset, width() - 2 * inset, height() - 2 * inset);

    if (m_chrome && m_chrome->titleBar()) {
        const QPoint below =
            m_chrome->titleBar()->mapTo(this, QPoint(0, m_chrome->titleBar()->height()));
        if (below.y() + margin > bounds.top()) bounds.setTop(below.y() + margin);
    }
    if (m_statusBar && m_statusBar->isVisible()) {
        const int statusTop = m_statusBar->mapTo(this, QPoint(0, 0)).y();
        if (statusTop - margin < bounds.bottom()) bounds.setBottom(statusTop - margin);
    }

    m_tempLabel->setMovableBounds(bounds);

    if (!m_tempLabel->isUserPlaced()) {
        m_tempLabel->autoFit();
    } else {
        // Re-anchor bottom-left at the user's size, or it strands mid-window on growth.
        QSize wanted = m_tempLabel->size();
        wanted.setWidth(qMin(wanted.width(), bounds.width()));
        wanted.setHeight(qMin(wanted.height(), bounds.height()));
        m_tempLabel->resize(wanted);
        m_tempLabel->move(bounds.left(), bounds.bottom() - wanted.height() + 1);
    }
    m_tempLabel->raise();
}

void PreviewPopoutWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    loadNewestTempImage();
    if (windowOpacity() < 0.99)
        propertyAnimate(this, "windowOpacity", windowOpacity(), 1.0, 200,
                        QEasingCurve::InOutSine);
}

void PreviewPopoutWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && isFullScreen()) {
        QPropertyAnimation* out = propertyAnimate(this, "windowOpacity", windowOpacity(), 0.0,
                                                  200, QEasingCurve::InOutSine);
        connect(out, &QPropertyAnimation::finished, this, [this]() {
            showNormal();
            propertyAnimate(this, "windowOpacity", windowOpacity(), 1.0, 200,
                            QEasingCurve::InOutSine);
        });
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void PreviewPopoutWindow::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() != QEvent::WindowStateChange) return;

    auto* stateChange = static_cast<QWindowStateChangeEvent*>(event);
    const bool wasMinimized = stateChange->oldState() & Qt::WindowMinimized;
    const bool isMinimized = windowState() & Qt::WindowMinimized;
    if (wasMinimized && !isMinimized && windowOpacity() < 0.99)
        propertyAnimate(this, "windowOpacity", windowOpacity(), 1.0, 200,
                        QEasingCurve::InOutSine);

    if (m_chrome) m_chrome->onWindowStateChanged();
}

void PreviewPopoutWindow::closeEvent(QCloseEvent* event)
{
    // First pass fades out; the second closes.
    if (m_closing) {
        event->accept();
        return;
    }

    event->ignore();
    m_closing = true;

    QPropertyAnimation* out = propertyAnimate(this, "windowOpacity", windowOpacity(), 0.0, 200,
                                              QEasingCurve::InOutSine);
    connect(out, &QPropertyAnimation::finished, this, [this]() { close(); });
}

void PreviewPopoutWindow::loadNewestTempImage()
{
    if (m_tempFolder.isEmpty()) return;

    static const QStringList filters = {u"*.png"_s, u"*.jpg"_s, u"*.jpeg"_s, u"*.webp"_s};
    const QFileInfoList files = QDir(m_tempFolder).entryInfoList(filters, QDir::Files);
    if (files.isEmpty()) return;

    const QFileInfo* newest = &files[0];
    for (const QFileInfo& info : files)
        if (info.lastModified() > newest->lastModified()) newest = &info;

    const QString newestPath = newest->absoluteFilePath();

    // Skip a path already loaded.
    if (newestPath == m_lastTempPath) return;
    if (m_loadWatcher->isRunning()) return;

    m_lastTempPath = newestPath;
    m_loadWatcher->setFuture(
        QtConcurrent::run([path = newestPath]() -> QImage { return QImage(path); }));
}

} // namespace tc
