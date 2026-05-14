#include <gui/widgets/windowchrome.h>
#include <gui/widgets/titlebar.h>
#include <gui/widgets/framelesschrome.h>
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QMouseEvent>
#include <QRegion>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

using gui::framelesschrome::cursorForEdges;
using gui::framelesschrome::edgesAt;
using gui::framelesschrome::kResizeBorder;
using gui::framelesschrome::kResizeHit;
using gui::framelesschrome::ResizeOutline;

namespace gui {

WindowChrome::WindowChrome(QWidget* host) : WindowChrome(host, Options{}) {}

WindowChrome::WindowChrome(QWidget* host, Options opt) : QObject(host), m_host(host), m_opt(opt)
{
    // m_frame is the chrome wrapper. Its kResizeBorder layout margin creates
    // the visible cosmetic border; the dark fill comes from #MainFrame's QSS.
    m_frame = new QWidget(host);
    m_frame->setObjectName("MainFrame");
    m_frame->setAttribute(Qt::WA_StyledBackground, true);
    m_frame->installEventFilter(this);

    auto* fLayout = new QVBoxLayout(m_frame);
    fLayout->setContentsMargins(kResizeBorder, kResizeBorder, kResizeBorder, kResizeBorder);
    fLayout->setSpacing(0);

    m_titleBar = new TitleBar(m_frame);
    m_titleBar->setButtons(opt.showMin, opt.showMax, opt.showClose);

    m_body = new QWidget(m_frame);
    m_body->setObjectName("ChromedDialogBody");
    m_body->setAttribute(Qt::WA_StyledBackground, true);

    fLayout->addWidget(m_titleBar);
    fLayout->addWidget(m_body, 1);

    // Edge-resize hit ring on top of m_frame; the inner area is masked out
    // so it passes through to body widgets. Geometry/mask are resynced in
    // the eventFilter resize handler.
    m_resizeOverlay = new QWidget(m_frame);
    m_resizeOverlay->setObjectName("ResizeOverlay");
    m_resizeOverlay->setAttribute(Qt::WA_NoSystemBackground);
    m_resizeOverlay->setAttribute(Qt::WA_TranslucentBackground);
    m_resizeOverlay->setMouseTracking(true);
    m_resizeOverlay->installEventFilter(this);
    m_resizeOverlay->raise();

    // If the OS swallows our release event mid-drag (a chord/keybind click on
    // the user's mouse can do this by breaking Qt's implicit grab), neither
    // updateResizeOutline nor endResizeDrag would ever run again - the outline
    // and override cursor stay stuck. This poll catches it and aborts.
    m_dragGuardTimer = new QTimer(this);
    m_dragGuardTimer->setInterval(200);
    connect(m_dragGuardTimer, &QTimer::timeout, this, [this]() {
        if (!m_dragEdges) return;
        if (QApplication::mouseButtons() & Qt::LeftButton) return;
        // Revert to the pre-drag geometry rather than committing to wherever
        // the cursor happens to be now - the user wasn't actually dragging
        // when this fired.
        endResizeDrag(m_dragStartGlobal);
    });
}

void WindowChrome::onWindowStateChanged()
{
    if (!m_frame || !m_titleBar) return;
    const bool fullscreen = m_host && m_host->isFullScreen();
    const bool maximized = m_host && m_host->isMaximized();
    m_titleBar->setVisible(!fullscreen);
    if (auto* lay = m_frame->layout()) {
        const int b = (fullscreen || maximized) ? 0 : kResizeBorder;
        lay->setContentsMargins(b, b, b, b);
    }
    if (m_resizeOverlay) m_resizeOverlay->setVisible(!fullscreen && !maximized);
}

bool WindowChrome::eventFilter(QObject* obj, QEvent* event)
{
    // Keep the resize overlay sized to m_frame and re-cut its mask so only
    // the kResizeHit-wide outer ring is mouse-active.
    if (obj == m_frame && event->type() == QEvent::Resize && m_resizeOverlay) {
        m_resizeOverlay->setGeometry(m_frame->rect());
        m_resizeOverlay->raise();
        const QRect r = m_resizeOverlay->rect();
        if (r.width() > 2 * kResizeHit && r.height() > 2 * kResizeHit) {
            const QRegion full(r);
            const QRegion inner(r.adjusted(kResizeHit, kResizeHit, -kResizeHit, -kResizeHit));
            m_resizeOverlay->setMask(full - inner);
        }
        else {
            m_resizeOverlay->clearMask();
        }
    }

    // Outline-style edge resize; off in maximized/fullscreen since the OS
    // owns geometry there.
    if (obj == m_resizeOverlay && m_host && !m_host->isMaximized() && !m_host->isFullScreen()) {
        if (event->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (m_dragEdges) {
                updateResizeOutline(me->globalPosition().toPoint());
            }
            else {
                const Qt::Edges e = edgesAt(me->position().toPoint(), m_resizeOverlay->size());
                if (e)
                    m_resizeOverlay->setCursor(cursorForEdges(e));
                else
                    m_resizeOverlay->unsetCursor();
            }
        }
        else if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                const Qt::Edges e = edgesAt(me->position().toPoint(), m_resizeOverlay->size());
                if (e) {
                    beginResizeDrag(e, me->globalPosition().toPoint());
                    return true;
                }
            }
        }
        else if (event->type() == QEvent::MouseButtonRelease) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (m_dragEdges && me->button() == Qt::LeftButton) {
                endResizeDrag(me->globalPosition().toPoint());
                return true;
            }
        }
        else if (event->type() == QEvent::Leave) {
            if (!m_dragEdges) m_resizeOverlay->unsetCursor();
        }
    }

    return QObject::eventFilter(obj, event);
}

void WindowChrome::beginResizeDrag(Qt::Edges edges, const QPoint& globalStart)
{
    // Re-entry guard: a second press arriving while a drag is live (sidebutton
    // / chord / keybind-simulated click) would push another override cursor
    // and reset the start geometry. The guard-timer cleans up if the existing
    // drag is actually stale.
    if (m_dragEdges) return;

    m_dragEdges = edges;
    m_dragStartGeo = m_host ? m_host->geometry() : QRect();
    m_dragStartGlobal = globalStart;

    if (!m_resizeOutline) m_resizeOutline = new ResizeOutline();
    m_resizeOutline->setGeometry(m_dragStartGeo);
    m_resizeOutline->show();
    m_resizeOutline->raise();

    // Pin the cursor app-wide; the mouse routinely leaves the overlay
    // while the user pulls past the old window edge.
    QApplication::setOverrideCursor(QCursor(cursorForEdges(edges)));

    // QDialog::exec()'s modal loop breaks the implicit mouse grab a press
    // would normally hold on m_resizeOverlay, so we grab explicitly.
    if (m_opt.modalGrab) m_resizeOverlay->grabMouse();

    if (m_dragGuardTimer) m_dragGuardTimer->start();
}

void WindowChrome::updateResizeOutline(const QPoint& globalNow)
{
    if (!m_dragEdges || !m_resizeOutline) return;
    m_resizeOutline->setGeometry(computeResizeGeometry(globalNow));
}

void WindowChrome::endResizeDrag(const QPoint& globalNow)
{
    if (!m_dragEdges) return;
    if (m_dragGuardTimer) m_dragGuardTimer->stop();
    if (m_opt.modalGrab && m_resizeOverlay) m_resizeOverlay->releaseMouse();
    const QRect target = computeResizeGeometry(globalNow);
    if (m_resizeOutline) m_resizeOutline->hide();
    m_dragEdges = Qt::Edges{};
    QApplication::restoreOverrideCursor();
    if (m_host) m_host->setGeometry(target);
}

QRect WindowChrome::computeResizeGeometry(const QPoint& globalNow) const
{
    QRect g = m_dragStartGeo;
    const QPoint d = globalNow - m_dragStartGlobal;
    if (m_dragEdges & Qt::LeftEdge) g.setLeft(g.left() + d.x());
    if (m_dragEdges & Qt::RightEdge) g.setRight(g.right() + d.x());
    if (m_dragEdges & Qt::TopEdge) g.setTop(g.top() + d.y());
    if (m_dragEdges & Qt::BottomEdge) g.setBottom(g.bottom() + d.y());

    QSize minSz(320, 200);
    if (m_host) {
        minSz =
            m_host->minimumSizeHint().expandedTo(m_host->minimumSize()).expandedTo(QSize(320, 200));
    }
    if (g.width() < minSz.width()) {
        if (m_dragEdges & Qt::LeftEdge)
            g.setLeft(g.right() - minSz.width() + 1);
        else
            g.setRight(g.left() + minSz.width() - 1);
    }
    if (g.height() < minSz.height()) {
        if (m_dragEdges & Qt::TopEdge)
            g.setTop(g.bottom() - minSz.height() + 1);
        else
            g.setBottom(g.top() + minSz.height() - 1);
    }
    return g;
}

} // namespace gui
