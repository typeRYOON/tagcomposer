#include <app/window_chrome.h>
#include <app/frameless_chrome.h>
#include <app/title_bar.h>
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QMouseEvent>
#include <QRegion>
#include <QTimer>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;
using tc::chrome::cursorForEdges;
using tc::chrome::edgesAt;
using tc::chrome::kResizeBorder;
using tc::chrome::kResizeHit;
using tc::chrome::ResizeOutline;

namespace tc {

WindowChrome::WindowChrome(QWidget* host) : WindowChrome(host, Options{}) {}

WindowChrome::WindowChrome(QWidget* host, Options options)
    : QObject(host), m_host(host), m_options(options)
{
    m_frame = new QWidget(host);
    m_frame->setObjectName(u"MainFrame"_s);
    m_frame->setAttribute(Qt::WA_StyledBackground, true);
    m_frame->installEventFilter(this);

    auto* layout = new QVBoxLayout(m_frame);
    layout->setContentsMargins(kResizeBorder, kResizeBorder, kResizeBorder, kResizeBorder);
    layout->setSpacing(0);

    m_titleBar = new TitleBar(m_frame);
    m_titleBar->setButtons(options.showMin, options.showMax, options.showClose);

    m_body = new QWidget(m_frame);
    m_body->setObjectName(u"ChromedBody"_s);
    m_body->setAttribute(Qt::WA_StyledBackground, true);

    layout->addWidget(m_titleBar);
    layout->addWidget(m_body, 1);

    // Edge hit ring; the masked-out middle passes clicks through.
    m_resizeOverlay = new QWidget(m_frame);
    m_resizeOverlay->setObjectName(u"ResizeOverlay"_s);
    m_resizeOverlay->setAttribute(Qt::WA_NoSystemBackground);
    m_resizeOverlay->setAttribute(Qt::WA_TranslucentBackground);
    m_resizeOverlay->setMouseTracking(true);
    m_resizeOverlay->installEventFilter(this);
    m_resizeOverlay->raise();

    m_dragGuard = new QTimer(this);
    m_dragGuard->setInterval(200);
    connect(m_dragGuard, &QTimer::timeout, this, [this]() {
        if (!m_dragEdges) return;
        if (QApplication::mouseButtons() & Qt::LeftButton) return;
        // The release was missed; revert.
        endResizeDrag(m_dragStartGlobal);
    });
}

QWidget* WindowChrome::frame() const
{
    return m_frame;
}

QWidget* WindowChrome::body() const
{
    return m_body;
}

TitleBar* WindowChrome::titleBar() const
{
    return m_titleBar;
}

void WindowChrome::onWindowStateChanged()
{
    const bool fullscreen = m_host && m_host->isFullScreen();
    const bool maximized = m_host && m_host->isMaximized();

    m_titleBar->setVisible(!fullscreen);
    if (QLayout* layout = m_frame->layout()) {
        const int border = (fullscreen || maximized) ? 0 : kResizeBorder;
        layout->setContentsMargins(border, border, border, border);
    }
    m_resizeOverlay->setVisible(!fullscreen && !maximized);
}

bool WindowChrome::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_frame && event->type() == QEvent::Resize) {
        m_resizeOverlay->setGeometry(m_frame->rect());
        m_resizeOverlay->raise();

        const QRect r = m_resizeOverlay->rect();
        if (r.width() > 2 * kResizeHit && r.height() > 2 * kResizeHit) {
            const QRegion outer(r);
            const QRegion inner(r.adjusted(kResizeHit, kResizeHit, -kResizeHit, -kResizeHit));
            m_resizeOverlay->setMask(outer - inner);
        }
        else {
            m_resizeOverlay->clearMask();
        }
    }

    // The OS owns geometry when maximized or fullscreen.
    if (obj == m_resizeOverlay && m_host && !m_host->isMaximized() && !m_host->isFullScreen()) {
        auto* mouse = static_cast<QMouseEvent*>(event);

        switch (event->type()) {
        case QEvent::MouseMove:
            if (m_dragEdges) {
                updateResizeOutline(mouse->globalPosition().toPoint());
            }
            else if (const Qt::Edges edges =
                         edgesAt(mouse->position().toPoint(), m_resizeOverlay->size())) {
                m_resizeOverlay->setCursor(cursorForEdges(edges));
            }
            else {
                m_resizeOverlay->unsetCursor();
            }
            break;

        case QEvent::MouseButtonPress:
            if (mouse->button() == Qt::LeftButton) {
                if (const Qt::Edges edges =
                        edgesAt(mouse->position().toPoint(), m_resizeOverlay->size())) {
                    beginResizeDrag(edges, mouse->globalPosition().toPoint());
                    return true;
                }
            }
            break;

        case QEvent::MouseButtonRelease:
            if (m_dragEdges && mouse->button() == Qt::LeftButton) {
                endResizeDrag(mouse->globalPosition().toPoint());
                return true;
            }
            break;

        case QEvent::Leave:
            if (!m_dragEdges) m_resizeOverlay->unsetCursor();
            break;

        default:
            break;
        }
    }

    return QObject::eventFilter(obj, event);
}

void WindowChrome::beginResizeDrag(Qt::Edges edges, const QPoint& globalStart)
{
    // Ignore a second press mid-drag; the guard timer ends stale drags.
    if (m_dragEdges) return;

    m_dragEdges = edges;
    m_dragStartGeometry = m_host ? m_host->geometry() : QRect();
    m_dragStartGlobal = globalStart;

    if (!m_resizeOutline) m_resizeOutline = new ResizeOutline();
    m_resizeOutline->setGeometry(m_dragStartGeometry);
    m_resizeOutline->show();
    m_resizeOutline->raise();

    // App-wide: the cursor leaves the overlay mid-drag.
    QApplication::setOverrideCursor(QCursor(cursorForEdges(edges)));
    if (m_options.modalGrab) m_resizeOverlay->grabMouse();

    m_dragGuard->start();
}

void WindowChrome::updateResizeOutline(const QPoint& globalNow)
{
    if (m_dragEdges && m_resizeOutline) m_resizeOutline->setGeometry(resizeGeometry(globalNow));
}

void WindowChrome::endResizeDrag(const QPoint& globalNow)
{
    if (!m_dragEdges) return;

    m_dragGuard->stop();
    if (m_options.modalGrab) m_resizeOverlay->releaseMouse();

    const QRect target = resizeGeometry(globalNow);
    if (m_resizeOutline) m_resizeOutline->hide();

    m_dragEdges = Qt::Edges{};
    QApplication::restoreOverrideCursor();
    if (m_host) m_host->setGeometry(target);
}

QRect WindowChrome::resizeGeometry(const QPoint& globalNow) const
{
    QRect geometry = m_dragStartGeometry;
    const QPoint delta = globalNow - m_dragStartGlobal;

    if (m_dragEdges & Qt::LeftEdge) geometry.setLeft(geometry.left() + delta.x());
    if (m_dragEdges & Qt::RightEdge) geometry.setRight(geometry.right() + delta.x());
    if (m_dragEdges & Qt::TopEdge) geometry.setTop(geometry.top() + delta.y());
    if (m_dragEdges & Qt::BottomEdge) geometry.setBottom(geometry.bottom() + delta.y());

    QSize minimum(320, 200);
    if (m_host)
        minimum = m_host->minimumSizeHint().expandedTo(m_host->minimumSize()).expandedTo(minimum);

    if (geometry.width() < minimum.width()) {
        if (m_dragEdges & Qt::LeftEdge)
            geometry.setLeft(geometry.right() - minimum.width() + 1);
        else
            geometry.setRight(geometry.left() + minimum.width() - 1);
    }
    if (geometry.height() < minimum.height()) {
        if (m_dragEdges & Qt::TopEdge)
            geometry.setTop(geometry.bottom() - minimum.height() + 1);
        else
            geometry.setBottom(geometry.top() + minimum.height() - 1);
    }

    return geometry;
}

} // namespace tc
