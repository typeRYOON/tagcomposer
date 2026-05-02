#include <gui/composer/previewpopoutwindow.h>
#include <gui/composer/scaledimagelabel.h>
#include <gui/composer/clickablelabel.h>
#include <gui/widgets/framelesschrome.h>
#include <gui/widgets/titlebar.h>
#include <utils/qutils.h>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QImage>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QRegion>
#include <QResizeEvent>
#include <QShortcut>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
#include <QWindowStateChangeEvent>
#include <QtConcurrent>

using gui::framelesschrome::kResizeBorder;
using gui::framelesschrome::kResizeHit;
using gui::framelesschrome::edgesAt;
using gui::framelesschrome::cursorForEdges;
using gui::framelesschrome::ResizeOutline;

namespace gui {

PreviewPopoutWindow::PreviewPopoutWindow(QWidget* parent)
    : QWidget(parent,
              Qt::Window | Qt::FramelessWindowHint
              | Qt::WindowMinimizeButtonHint
              | Qt::WindowMaximizeButtonHint
              | Qt::WindowCloseButtonHint)
{
    setObjectName("PreviewPopout");
    setWindowTitle("Preview");
    resize(900, 700);
    setMinimumSize(400, 300);
    setAttribute(Qt::WA_StyledBackground);
    setWindowOpacity(0.0);  // fade in on first showEvent

    m_imageLabel = new ScaledImageLabel(this);

    // Build the chrome the same way AppMainWindow does: TitleBar at top,
    // content (image) below, with a thin cosmetic border around them via
    // m_frame's layout margin, and a masked overlay catching the resize
    // hit zone.
    QWidget* content = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(m_imageLabel);

    m_titleBar = new TitleBar(this);

    m_frame = new QWidget(this);
    m_frame->setObjectName("MainFrame");
    m_frame->setAttribute(Qt::WA_StyledBackground, true);
    m_frame->installEventFilter(this);  // Resize → reshape overlay

    auto* fLayout = new QVBoxLayout(m_frame);
    fLayout->setContentsMargins(kResizeBorder, kResizeBorder,
                                kResizeBorder, kResizeBorder);
    fLayout->setSpacing(0);
    fLayout->addWidget(m_titleBar);
    fLayout->addWidget(content, 1);

    m_resizeOverlay = new QWidget(m_frame);
    m_resizeOverlay->setObjectName("ResizeOverlay");
    m_resizeOverlay->setAttribute(Qt::WA_NoSystemBackground);
    m_resizeOverlay->setAttribute(Qt::WA_TranslucentBackground);
    m_resizeOverlay->setMouseTracking(true);
    m_resizeOverlay->installEventFilter(this);
    m_resizeOverlay->raise();

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(m_frame);

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

    // Per-window shortcuts. Main window's shortcuts are also WindowShortcut,
    // so each window owns its own copies of these keys with no ambiguity.
    auto addShortcut = [this](const QString& seq, auto handler) {
        auto* a = new QAction(this);
        a->setShortcut(QKeySequence(seq));
        a->setShortcutContext(Qt::WindowShortcut);
        connect(a, &QAction::triggered, this, handler);
        addAction(a);
    };

    addShortcut("F11", [this]() {
        auto* anim = utils::propertyAnimate(this, "windowOpacity",
            windowOpacity(), 0.0, 200, QEasingCurve::InOutSine);
        connect(anim, &QPropertyAnimation::finished, this, [this]() {
            if (isFullScreen()) showNormal();
            else                showFullScreen();
            utils::propertyAnimate(this, "windowOpacity",
                windowOpacity(), 1.0, 200, QEasingCurve::InOutSine);
        });
    });
    addShortcut("Shift+E",     [this]() { emit runRequested(); });
    addShortcut("Shift+R",     [this]() { emit interruptRequested(); });
    addShortcut("Shift+Alt+R", [this]() { emit clearPendingRequested(); });
    addShortcut("Ctrl+W",      [this]() { close(); });
    addShortcut("Ctrl+H",      [this]() {
        if (windowState() & Qt::WindowMinimized) return;
        auto* anim = utils::propertyAnimate(this, "windowOpacity",
            windowOpacity(), 0.0, 200, QEasingCurve::InOutSine);
        connect(anim, &QPropertyAnimation::finished, this, [this]() {
            showMinimized();
        });
    });
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
    // Bottom-left of the visible content area. Inset by kResizeBorder so the
    // label doesn't sit on top of the cosmetic frame border.
    m_tempLabel->move(margin + kResizeBorder,
                      height() - side - margin - kResizeBorder);
    m_tempLabel->raise();
}

void PreviewPopoutWindow::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    loadNewestTempImage();
    if (windowOpacity() < 0.99) {
        utils::propertyAnimate(this, "windowOpacity",
                               windowOpacity(), 1.0, 200, QEasingCurve::InOutSine);
    }
}

void PreviewPopoutWindow::keyPressEvent(QKeyEvent* e)
{
    // Esc only fires here if no focused child consumed it first.
    if (e->key() == Qt::Key_Escape && isFullScreen()) {
        auto* anim = utils::propertyAnimate(this, "windowOpacity",
            windowOpacity(), 0.0, 200, QEasingCurve::InOutSine);
        connect(anim, &QPropertyAnimation::finished, this, [this]() {
            showNormal();
            utils::propertyAnimate(this, "windowOpacity",
                windowOpacity(), 1.0, 200, QEasingCurve::InOutSine);
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
    const bool wasMinimized = (ev->oldState()  & Qt::WindowMinimized);
    const bool isMinimized  = (windowState()   & Qt::WindowMinimized);
    if (wasMinimized && !isMinimized && windowOpacity() < 0.99) {
        utils::propertyAnimate(this, "windowOpacity",
            windowOpacity(), 1.0, 200, QEasingCurve::InOutSine);
    }

    // Frameless chrome adapts to window state — same pattern as AppMainWindow.
    if (m_frame && m_titleBar) {
        const bool fullscreen = isFullScreen();
        const bool maximized  = isMaximized();
        m_titleBar->setVisible(!fullscreen);
        if (auto* lay = m_frame->layout()) {
            const int b = (fullscreen || maximized) ? 0 : kResizeBorder;
            lay->setContentsMargins(b, b, b, b);
        }
        if (m_resizeOverlay) m_resizeOverlay->setVisible(!fullscreen && !maximized);
    }
}

void PreviewPopoutWindow::closeEvent(QCloseEvent* e)
{
    // First close → swallow event, fade out, then re-close which lets through.
    // Mirror of AppMainWindow's closeEvent fade pattern.
    if (m_isClosing) {
        e->accept();
        return;
    }
    e->ignore();
    m_isClosing = true;
    auto* anim = utils::propertyAnimate(this, "windowOpacity",
                                        windowOpacity(), 0.0, 200, QEasingCurve::InOutSine);
    connect(anim, &QPropertyAnimation::finished, this, [this]() { close(); });
}

bool PreviewPopoutWindow::eventFilter(QObject* obj, QEvent* event)
{
    // Keep the resize overlay sized to m_frame and re-cut its mask so only
    // the kResizeHit-wide outer ring is mouse-active.
    if (obj == m_frame && event->type() == QEvent::Resize && m_resizeOverlay) {
        m_resizeOverlay->setGeometry(m_frame->rect());
        m_resizeOverlay->raise();
        const QRect r = m_resizeOverlay->rect();
        if (r.width() > 2 * kResizeHit && r.height() > 2 * kResizeHit) {
            const QRegion full(r);
            const QRegion inner(r.adjusted(kResizeHit, kResizeHit,
                                          -kResizeHit, -kResizeHit));
            m_resizeOverlay->setMask(full - inner);
        } else {
            m_resizeOverlay->clearMask();
        }
    }

    // Frameless edge-resize: outline preview during drag, commit geometry on
    // release. Qt auto-grabs the mouse to m_resizeOverlay between press and
    // release, so move/release events keep coming here even when the cursor
    // is outside the window.
    if (obj == m_resizeOverlay && !isMaximized() && !isFullScreen()) {
        if (event->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (m_dragEdges) {
                updateResizeOutline(me->globalPosition().toPoint());
            } else {
                const Qt::Edges e = edgesAt(me->position().toPoint(),
                                            m_resizeOverlay->size());
                if (e) m_resizeOverlay->setCursor(cursorForEdges(e));
                else   m_resizeOverlay->unsetCursor();
            }
        } else if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                const Qt::Edges e = edgesAt(me->position().toPoint(),
                                            m_resizeOverlay->size());
                if (e) {
                    beginResizeDrag(e, me->globalPosition().toPoint());
                    return true;
                }
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (m_dragEdges && me->button() == Qt::LeftButton) {
                endResizeDrag(me->globalPosition().toPoint());
                return true;
            }
        } else if (event->type() == QEvent::Leave) {
            if (!m_dragEdges) m_resizeOverlay->unsetCursor();
        }
    }

    return QWidget::eventFilter(obj, event);
}

void PreviewPopoutWindow::beginResizeDrag(Qt::Edges edges, const QPoint& globalStart)
{
    m_dragEdges       = edges;
    m_dragStartGeo    = geometry();
    m_dragStartGlobal = globalStart;

    if (!m_resizeOutline) m_resizeOutline = new ResizeOutline();
    m_resizeOutline->setGeometry(m_dragStartGeo);
    m_resizeOutline->show();
    m_resizeOutline->raise();

    QApplication::setOverrideCursor(QCursor(cursorForEdges(edges)));
}

void PreviewPopoutWindow::updateResizeOutline(const QPoint& globalNow)
{
    if (!m_dragEdges || !m_resizeOutline) return;
    m_resizeOutline->setGeometry(computeResizeGeometry(globalNow));
}

void PreviewPopoutWindow::endResizeDrag(const QPoint& globalNow)
{
    if (!m_dragEdges) return;
    const QRect target = computeResizeGeometry(globalNow);
    if (m_resizeOutline) m_resizeOutline->hide();
    m_dragEdges = Qt::Edges{};
    QApplication::restoreOverrideCursor();
    setGeometry(target);
}

QRect PreviewPopoutWindow::computeResizeGeometry(const QPoint& globalNow) const
{
    QRect g = m_dragStartGeo;
    const QPoint d = globalNow - m_dragStartGlobal;
    if (m_dragEdges & Qt::LeftEdge)   g.setLeft  (g.left()   + d.x());
    if (m_dragEdges & Qt::RightEdge)  g.setRight (g.right()  + d.x());
    if (m_dragEdges & Qt::TopEdge)    g.setTop   (g.top()    + d.y());
    if (m_dragEdges & Qt::BottomEdge) g.setBottom(g.bottom() + d.y());

    const QSize minSz = minimumSizeHint().expandedTo(minimumSize())
                                         .expandedTo(QSize(320, 200));
    if (g.width() < minSz.width()) {
        if (m_dragEdges & Qt::LeftEdge) g.setLeft(g.right() - minSz.width() + 1);
        else                            g.setRight(g.left() + minSz.width() - 1);
    }
    if (g.height() < minSz.height()) {
        if (m_dragEdges & Qt::TopEdge)  g.setTop(g.bottom() - minSz.height() + 1);
        else                            g.setBottom(g.top() + minSz.height() - 1);
    }
    return g;
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
