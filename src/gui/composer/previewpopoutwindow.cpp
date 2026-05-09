#include <gui/composer/previewpopoutwindow.h>
#include <gui/composer/scaledimagelabel.h>
#include <gui/composer/clickablelabel.h>
#include <gui/widgets/framelesschrome.h>
#include <gui/widgets/statusbar.h>
#include <gui/widgets/titlebar.h>
#include <gui/widgets/windowchrome.h>
#include <utils/qutils.h>
#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QImage>
#include <QKeyEvent>
#include <QKeySequence>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QShortcut>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindowStateChangeEvent>
#include <QtConcurrent>

using gui::framelesschrome::kResizeBorder;

namespace gui {

PreviewPopoutWindow::PreviewPopoutWindow(QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinimizeButtonHint |
                          Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint)
{
    setObjectName("PreviewPopout");
    setWindowTitle("Preview");
    resize(900, 700);
    setMinimumSize(400, 300);
    setAttribute(Qt::WA_StyledBackground);
    setWindowOpacity(0.0); // fade in on first showEvent

    m_imageLabel = new ScaledImageLabel(this);

    // Non-modal top-level: full chrome with no explicit mouse-grab needed
    // (the implicit grab works fine outside QDialog::exec()).
    m_chrome = new WindowChrome(this);

    m_statusBar = new StatusBar(this);

    auto* contentLayout = new QVBoxLayout(m_chrome->bodyWidget());
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(m_imageLabel, 1);
    contentLayout->addWidget(m_statusBar);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(m_chrome->frame());

    m_tempLabel = new ClickableLabel(this);
    m_tempLabel->setObjectName("PopoutTempLabel");
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
        const QImage img = m_loadWatcher->result();
        if (!img.isNull()) {
            m_tempLabel->setSourcePixmap(QPixmap::fromImage(img));
            m_tempLabel->setFilePath(m_lastTempPath);
            m_tempLabel->show();
            m_tempLabel->raise();
        }
        // Coalescing backstop: a directoryChanged during the load bailed
        // early; the debounce + dedupe re-checks for anything newer.
        m_debounce->start();
    });

    // WindowShortcut so each window owns its own copy and there's no
    // ambiguity with the main window's identical bindings.
    auto addShortcut = [this](const QString& seq, auto handler) {
        auto* a = new QAction(this);
        a->setShortcut(QKeySequence(seq));
        a->setShortcutContext(Qt::WindowShortcut);
        connect(a, &QAction::triggered, this, handler);
        addAction(a);
    };

    addShortcut("F11", [this]() {
        auto* anim = utils::propertyAnimate(this, "windowOpacity", windowOpacity(), 0.0, 200,
                                            QEasingCurve::InOutSine);
        connect(anim, &QPropertyAnimation::finished, this, [this]() {
            if (isFullScreen())
                showNormal();
            else
                showFullScreen();
            utils::propertyAnimate(this, "windowOpacity", windowOpacity(), 1.0, 200,
                                   QEasingCurve::InOutSine);
        });
    });
    addShortcut("Shift+E", [this]() { emit runRequested(); });
    addShortcut("Shift+R", [this]() { emit interruptRequested(); });
    addShortcut("Shift+Alt+R", [this]() { emit clearPendingRequested(); });
    addShortcut("Ctrl+W", [this]() { close(); });
    addShortcut("Ctrl+H", [this]() {
        if (windowState() & Qt::WindowMinimized) return;
        auto* anim = utils::propertyAnimate(this, "windowOpacity", windowOpacity(), 0.0, 200,
                                            QEasingCurve::InOutSine);
        connect(anim, &QPropertyAnimation::finished, this, [this]() { showMinimized(); });
    });
}

void PreviewPopoutWindow::setImage(const QPixmap& pix)
{
    m_imageLabel->setSourcePixmap(pix);
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
    if (m_tempFolder == folder) return;
    if (!m_watcher->directories().isEmpty()) m_watcher->removePaths(m_watcher->directories());
    m_tempFolder = folder;
    if (!folder.isEmpty() && QDir(folder).exists()) m_watcher->addPath(folder);
}

void PreviewPopoutWindow::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    constexpr int margin = 12;

    // Movable bounds: the body area inside the cosmetic frame, below the
    // titlebar, with a small margin so the label can't kiss the edges.
    QRect bounds(kResizeBorder + margin, kResizeBorder + margin,
                 width() - 2 * (kResizeBorder + margin),
                 height() - 2 * (kResizeBorder + margin));
    if (m_chrome && m_chrome->titleBar()) {
        const QPoint tbBR = m_chrome->titleBar()->mapTo(
            this, QPoint(0, m_chrome->titleBar()->height()));
        if (tbBR.y() + margin > bounds.top()) bounds.setTop(tbBR.y() + margin);
    }
    m_tempLabel->setMovableBounds(bounds);

    if (!m_tempLabel->isUserPlaced()) {
        m_tempLabel->autoFit();
    }
    else {
        // Re-anchor to bottom-left of the new bounds while preserving the
        // user's chosen size. clampToBounds would leave the label stranded
        // mid-window when the popup grows; this keeps it tracking the
        // bottom edge regardless of resize direction.
        QSize s = m_tempLabel->size();
        s.setWidth(qMin(s.width(), bounds.width()));
        s.setHeight(qMin(s.height(), bounds.height()));
        m_tempLabel->resize(s);
        m_tempLabel->move(bounds.left(), bounds.bottom() - s.height() + 1);
    }
    m_tempLabel->raise();
}

void PreviewPopoutWindow::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    loadNewestTempImage();
    if (windowOpacity() < 0.99) {
        utils::propertyAnimate(this, "windowOpacity", windowOpacity(), 1.0, 200,
                               QEasingCurve::InOutSine);
    }
}

void PreviewPopoutWindow::keyPressEvent(QKeyEvent* e)
{
    // Esc only fires here if no focused child consumed it first.
    if (e->key() == Qt::Key_Escape && isFullScreen()) {
        auto* anim = utils::propertyAnimate(this, "windowOpacity", windowOpacity(), 0.0, 200,
                                            QEasingCurve::InOutSine);
        connect(anim, &QPropertyAnimation::finished, this, [this]() {
            showNormal();
            utils::propertyAnimate(this, "windowOpacity", windowOpacity(), 1.0, 200,
                                   QEasingCurve::InOutSine);
        });
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}

void PreviewPopoutWindow::changeEvent(QEvent* e)
{
    QWidget::changeEvent(e);
    if (e->type() != QEvent::WindowStateChange) return;
    auto* ev = static_cast<QWindowStateChangeEvent*>(e);
    const bool wasMinimized = (ev->oldState() & Qt::WindowMinimized);
    const bool isMinimized = (windowState() & Qt::WindowMinimized);
    if (wasMinimized && !isMinimized && windowOpacity() < 0.99) {
        utils::propertyAnimate(this, "windowOpacity", windowOpacity(), 1.0, 200,
                               QEasingCurve::InOutSine);
    }

    if (m_chrome) m_chrome->onWindowStateChanged();
}

void PreviewPopoutWindow::closeEvent(QCloseEvent* e)
{
    // Two-pass close: first swallow + fade, second pass lets through.
    // Same pattern as AppMainWindow's closeEvent.
    if (m_isClosing) {
        e->accept();
        return;
    }
    e->ignore();
    m_isClosing = true;
    auto* anim = utils::propertyAnimate(this, "windowOpacity", windowOpacity(), 0.0, 200,
                                        QEasingCurve::InOutSine);
    connect(anim, &QPropertyAnimation::finished, this, [this]() { close(); });
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

    const QString newestPath = newest->absoluteFilePath();
    // Skip a re-decode when the load-finished handler's debounce-backstop
    // re-fires us against the same path.
    if (newestPath == m_lastTempPath) return;
    if (m_loadWatcher->isRunning()) return;
    m_lastTempPath = newestPath;
    m_loadWatcher->setFuture(
        QtConcurrent::run([path = newestPath]() -> QImage { return QImage(path); }));
}

} // namespace gui
