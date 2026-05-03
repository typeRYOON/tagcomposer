#include <gui/widgets/windowchrome.h>
#include <gui/widgets/titlebar.h>
#include <gui/widgets/framelesschrome.h>
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QMouseEvent>
#include <QRegion>
#include <QVBoxLayout>
#include <QWidget>

using gui::framelesschrome::kResizeBorder;
using gui::framelesschrome::kResizeHit;
using gui::framelesschrome::edgesAt;
using gui::framelesschrome::cursorForEdges;
using gui::framelesschrome::ResizeOutline;

namespace gui {

WindowChrome::WindowChrome(QWidget* host, Options opt)
    : QObject(host), m_host(host), m_opt(opt)
{
    // m_frame is the chrome wrapper. Its kResizeBorder layout margin creates
    // the visible cosmetic border; the dark fill comes from #MainFrame's QSS.
    m_frame = new QWidget(host);
    m_frame->setObjectName("MainFrame");
    m_frame->setAttribute(Qt::WA_StyledBackground, true);
    m_frame->installEventFilter(this);

    auto* fLayout = new QVBoxLayout(m_frame);
    fLayout->setContentsMargins(kResizeBorder, kResizeBorder,
                                kResizeBorder, kResizeBorder);
    fLayout->setSpacing(0);

    m_titleBar = new TitleBar(m_frame);
    m_titleBar->setButtons(opt.showMin, opt.showMax, opt.showClose);

    m_body = new QWidget(m_frame);
    m_body->setObjectName("ChromedDialogBody");
    m_body->setAttribute(Qt::WA_StyledBackground, true);

    fLayout->addWidget(m_titleBar);
    fLayout->addWidget(m_body, 1);

    // Resize hit-test overlay: a kResizeHit-wide ring on top of m_frame.
    // setMask carves out the inner area so events there pass through to the
    // body widgets — only the ring intercepts. Geometry & mask are kept in
    // sync with m_frame via the resize handler in eventFilter().
    m_resizeOverlay = new QWidget(m_frame);
    m_resizeOverlay->setObjectName("ResizeOverlay");
    m_resizeOverlay->setAttribute(Qt::WA_NoSystemBackground);
    m_resizeOverlay->setAttribute(Qt::WA_TranslucentBackground);
    m_resizeOverlay->setMouseTracking(true);
    m_resizeOverlay->installEventFilter(this);
    m_resizeOverlay->raise();
}

void WindowChrome::onWindowStateChanged()
{
    if (!m_frame || !m_titleBar) return;
    const bool fullscreen = m_host && m_host->isFullScreen();
    const bool maximized  = m_host && m_host->isMaximized();
    m_titleBar->setVisible(!fullscreen);
    if (auto* lay = m_frame->layout()) {
        const int b = (fullscreen || maximized) ? 0 : kResizeBorder;
        lay->setContentsMargins(b, b, b, b);
    }
    if (m_resizeOverlay)
        m_resizeOverlay->setVisible(!fullscreen && !maximized);
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
            const QRegion inner(r.adjusted(kResizeHit, kResizeHit,
                                          -kResizeHit, -kResizeHit));
            m_resizeOverlay->setMask(full - inner);
        } else {
            m_resizeOverlay->clearMask();
        }
    }

    // Outline-style edge resize. Disabled while maximized/fullscreen — the
    // OS owns geometry in those states, so dragging shouldn't reflow the
    // window.
    if (obj == m_resizeOverlay
        && m_host
        && !m_host->isMaximized()
        && !m_host->isFullScreen())
    {
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
            // Don't reset the cursor mid-drag — the cursor naturally leaves
            // the overlay when the user pulls past the old window edge.
            if (!m_dragEdges) m_resizeOverlay->unsetCursor();
        }
    }

    return QObject::eventFilter(obj, event);
}

void WindowChrome::beginResizeDrag(Qt::Edges edges, const QPoint& globalStart)
{
    m_dragEdges       = edges;
    m_dragStartGeo    = m_host ? m_host->geometry() : QRect();
    m_dragStartGlobal = globalStart;

    if (!m_resizeOutline) m_resizeOutline = new ResizeOutline();
    m_resizeOutline->setGeometry(m_dragStartGeo);
    m_resizeOutline->show();
    m_resizeOutline->raise();

    // Pin the resize cursor app-wide for the duration of the drag — the
    // mouse routinely leaves m_resizeOverlay while the user pulls beyond
    // the old window edge.
    QApplication::setOverrideCursor(QCursor(cursorForEdges(edges)));

    // QDialog::exec()'s application-modal event loop breaks the implicit
    // mouse grab the press would normally establish on m_resizeOverlay
    // (showing the ResizeOutline as another top-level widget is what
    // disrupts it). Without an explicit grab, MouseMove/MouseRelease never
    // come back through our event filter. Non-modal hosts don't need this.
    if (m_opt.modalGrab) m_resizeOverlay->grabMouse();
}

void WindowChrome::updateResizeOutline(const QPoint& globalNow)
{
    if (!m_dragEdges || !m_resizeOutline) return;
    m_resizeOutline->setGeometry(computeResizeGeometry(globalNow));
}

void WindowChrome::endResizeDrag(const QPoint& globalNow)
{
    if (!m_dragEdges) return;
    if (m_opt.modalGrab && m_resizeOverlay) m_resizeOverlay->releaseMouse();
    const QRect target = computeResizeGeometry(globalNow);
    qDebug() << target;
    if (m_resizeOutline) m_resizeOutline->hide();
    m_dragEdges = Qt::Edges{};
    QApplication::restoreOverrideCursor();
    if (m_host) m_host->setGeometry(target);
}

QRect WindowChrome::computeResizeGeometry(const QPoint& globalNow) const
{
    QRect g = m_dragStartGeo;
    const QPoint d = globalNow - m_dragStartGlobal;
    if (m_dragEdges & Qt::LeftEdge)   g.setLeft  (g.left()   + d.x());
    if (m_dragEdges & Qt::RightEdge)  g.setRight (g.right()  + d.x());
    if (m_dragEdges & Qt::TopEdge)    g.setTop   (g.top()    + d.y());
    if (m_dragEdges & Qt::BottomEdge) g.setBottom(g.bottom() + d.y());

    // Clamp to the host's minimum size. When dragging from top/left, pin the
    // moving edge so the opposite edge stays put.
    QSize minSz(320, 200);
    if (m_host) {
        minSz = m_host->minimumSizeHint()
                    .expandedTo(m_host->minimumSize())
                    .expandedTo(QSize(320, 200));
    }
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

} // namespace gui
