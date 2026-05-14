#include <gui/composer/statesgridview.h>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QImage>
#include <QLinearGradient>
#include <QMetaObject>
#include <QMimeData>
#include <QMouseEvent>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPixmap>
#include <QResizeEvent>
#include <QThreadPool>
#include <QUrl>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace gui {

namespace {

// Same tile recipe as the entry view: rounded-corner letterboxed image with a
// fade-to-black gradient at the bottom + title text overlaid. Dark backdrop
// matches the ComposerPreviewLabel chrome so empty / portrait tiles still
// look intentional.
QImage composeTile(const QImage& src, const QString& name, int side,
                   qreal gradStart, int gradAlpha, const QColor& titleColor)
{
    constexpr int Radius = 4;
    constexpr int TitleH = 26;

    QImage out(side, side, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);

    QPainter p(&out);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, side, side), Radius, Radius);
    p.setClipPath(clip);

    p.fillRect(QRect(0, 0, side, side), QColor("#0a0a0a"));

    if (!src.isNull()) {
        const QImage scaled =
            src.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const int x = (side - scaled.width()) / 2;
        const int y = (side - scaled.height()) / 2;
        p.drawImage(QPoint(x, y), scaled);
    }

    const qreal startY = side * gradStart;
    QLinearGradient grad(0, startY, 0, side);
    grad.setColorAt(0.0, QColor(0, 0, 0, 0));
    grad.setColorAt(1.0, QColor(0, 0, 0, gradAlpha));
    p.fillRect(QRectF(0, startY, side, side - startY), grad);

    const QStringList families = QFontDatabase::applicationFontFamilies(0);
    QFont f = families.isEmpty() ? QGuiApplication::font() : QFont(families.first());
    f.setPointSize(13);
    f.setHintingPreference(QFont::PreferFullHinting);
    f.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(f);
    p.setPen(titleColor);
    QFontMetrics fm(f);
    const QString elided = fm.elidedText(name, Qt::ElideRight, side - 16);
    p.drawText(QRect(8, side - TitleH, side - 16, 22),
               Qt::AlignLeft | Qt::AlignVCenter, elided);

    p.setBrush(Qt::NoBrush);
    p.setPen(QColor("#1a1a1a"));
    p.drawRoundedRect(QRectF(0.5, 0.5, side - 1.0, side - 1.0), Radius, Radius);

    return out;
}

} // namespace

bool StatesGridView::isImagePath(const QString& path)
{
    const QString l = path.toLower();
    return l.endsWith(".jpg") || l.endsWith(".jpeg") || l.endsWith(".png") || l.endsWith(".webp");
}

StatesGridView::StatesGridView(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);

    m_pixCache.setMaxCost(200);

    // Drives smooth scroll easing + tile fade-ins, and unconditionally pokes
    // update() each tick. The unconditional update() is what makes the
    // parent QStackedWidget's opacity-effect fade-in actually paint across
    // re-shows (Qt's own propagation through the effect chain isn't reliable
    // for this hierarchy). Gated on visibility via show/hideEvent so we
    // don't burn CPU while in composer mode.
    m_animTimer = new QTimer(this);
    m_animTimer->setInterval(16);
    connect(m_animTimer, &QTimer::timeout, this, [this]() {
        const qreal scrollDiff = m_scrollYTarget - m_scrollYActual;
        if (std::abs(scrollDiff) > 0.5)
            m_scrollYActual += scrollDiff * 0.18;
        else if (m_scrollYActual != m_scrollYTarget)
            m_scrollYActual = m_scrollYTarget;

        const qreal fadeStep = 16.0 / 200.0; // 200ms fade-in
        for (auto it = m_fadeIn.begin(); it != m_fadeIn.end(); ++it)
            if (it.value() < 1.0)
                it.value() = std::min(1.0, it.value() + fadeStep);

        update();
    });
    // Timer is started by showEvent; not running until the view is first shown.
}

void StatesGridView::setStates(const QList<core::SavedState>* states)
{
    m_states = states;
    ++m_generation;
    recomputeVisible();
    recomputeLayout();
    update();
}

void StatesGridView::setFilter(const QString& filter)
{
    if (m_filter == filter) return;
    m_filter = filter;
    ++m_generation;
    recomputeVisible();
    recomputeLayout();
    // Reset fade-in for everything currently in the result so each filter
    // change replays the tile fade (matches the entry view's query behavior,
    // but without dumping the pixmap cache - we just animate the visible set).
    if (m_states) {
        for (int modelIdx : m_visible)
            m_fadeIn[(*m_states)[modelIdx].id] = 0.0;
    }
    update();
}

void StatesGridView::setTileGradient(qreal start, int alpha)
{
    m_gradStart = std::clamp(start, 0.0, 1.0);
    m_gradAlpha = std::clamp(alpha, 0, 255);
    clearTileCache();
    update();
}

void StatesGridView::setTileTitleColor(const QColor& color)
{
    if (!color.isValid()) return;
    m_titleColor = color;
    clearTileCache();
    update();
}

void StatesGridView::setTileSize(int side)
{
    if (side <= 0 || side == m_tileSide) return;
    m_tileSide = side;
    clearTileCache();
    recomputeLayout();
    update();
}

void StatesGridView::invalidateTile(const QString& id)
{
    QMutexLocker lk(&m_cacheMutex);
    m_pixCache.remove(id);
    m_fadeIn.remove(id);
}

void StatesGridView::clearTileCache()
{
    QMutexLocker lk(&m_cacheMutex);
    m_pixCache.clear();
    m_fadeIn.clear();
}

bool StatesGridView::isEmpty() const
{
    return m_visible.isEmpty();
}

void StatesGridView::recomputeVisible()
{
    m_visible.clear();
    if (!m_states) return;
    for (int i = 0; i < m_states->size(); ++i) {
        if (m_filter.isEmpty() ||
            (*m_states)[i].name.contains(m_filter, Qt::CaseInsensitive)) {
            m_visible << i;
        }
    }
}

void StatesGridView::recomputeLayout()
{
    const int strideX = m_tileSide + m_spacing;
    const int strideY = m_tileSide + m_spacing;
    m_cols = std::max(1, (width() + m_spacing) / strideX);
    m_offsetX = std::max(0, (width() - (m_cols * m_tileSide + (m_cols - 1) * m_spacing)) / 2);
    const int rows = (m_visible.size() + m_cols - 1) / m_cols;
    m_totalH = rows * m_tileSide + std::max(0, rows - 1) * m_spacing + PadV * 2;

    const int maxScroll = std::max(0, m_totalH - height());
    if (m_scrollYTarget > maxScroll) m_scrollYTarget = maxScroll;
    if (m_scrollYActual > maxScroll) m_scrollYActual = maxScroll;
}

QRect StatesGridView::tileRect(int visIdx) const
{
    const int strideX = m_tileSide + m_spacing;
    const int strideY = m_tileSide + m_spacing;
    const int col = visIdx % m_cols;
    const int row = visIdx / m_cols;
    return QRect(m_offsetX + col * strideX,
                 PadV + row * strideY - static_cast<int>(m_scrollYActual),
                 m_tileSide, m_tileSide);
}

int StatesGridView::indexAtPoint(QPoint p) const
{
    if (m_cols <= 0) return -1;
    const int strideX = m_tileSide + m_spacing;
    const int strideY = m_tileSide + m_spacing;
    const int relX = p.x() - m_offsetX;
    const int relY = p.y() - PadV + static_cast<int>(m_scrollYActual);
    if (relX < 0 || relY < 0) return -1;
    const int col = relX / strideX;
    const int row = relY / strideY;
    if (col >= m_cols) return -1;
    // Check we're actually inside the tile, not its right/bottom gap.
    const int inTileX = relX - col * strideX;
    const int inTileY = relY - row * strideY;
    if (inTileX >= m_tileSide || inTileY >= m_tileSide) return -1;
    const int vis = row * m_cols + col;
    if (vis < 0 || vis >= m_visible.size()) return -1;
    return m_visible[vis];
}

QPixmap StatesGridView::renderTilePlaceholder(const QString& name) const
{
    return QPixmap::fromImage(
        composeTile(QImage(), name, m_tileSide, m_gradStart, m_gradAlpha, m_titleColor));
}

void StatesGridView::requestLoad(const QString& id, const QString& name,
                                 const QString& previewPath)
{
    {
        QMutexLocker lk(&m_cacheMutex);
        if (m_pending.contains(id)) return;
        m_pending.insert(id);
    }
    const int gen = m_generation;
    const int side = m_tileSide;
    const qreal gradStart = m_gradStart;
    const int gradAlpha = m_gradAlpha;
    const QColor titleColor = m_titleColor;

    QThreadPool::globalInstance()->start([this, id, name, previewPath, gen, side, gradStart,
                                          gradAlpha, titleColor]() {
        QImage img;
        if (!previewPath.isEmpty() && QFile::exists(previewPath)) img.load(previewPath);
        QImage composed = composeTile(img, name, side, gradStart, gradAlpha, titleColor);

        QMetaObject::invokeMethod(
            this,
            [this, id, gen, composed = std::move(composed)]() {
                if (gen != m_generation) {
                    QMutexLocker lk(&m_cacheMutex);
                    m_pending.remove(id);
                    return;
                }
                {
                    QMutexLocker lk(&m_cacheMutex);
                    m_pixCache.insert(id, new QPixmap(QPixmap::fromImage(composed)));
                    m_pending.remove(id);
                }
                m_fadeIn[id] = 0.0;
                update();
            },
            Qt::QueuedConnection);
    });
}

// ---- events

void StatesGridView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    if (!m_states || m_visible.isEmpty()) return;

    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    // Only paint tiles that are at least partially in the viewport.
    const int strideY = m_tileSide + m_spacing;
    const int firstRow =
        std::max(0, (static_cast<int>(m_scrollYActual) - PadV) / std::max(1, strideY));
    const int maxRow = static_cast<int>((m_visible.size() - 1) / std::max(1, m_cols));
    const int viewportLastRow =
        (static_cast<int>(m_scrollYActual) + height() - PadV) / std::max(1, strideY) + 1;
    const int lastRow = std::min(maxRow, viewportLastRow);

    for (int row = firstRow; row <= lastRow; ++row) {
        for (int col = 0; col < m_cols; ++col) {
            const int vis = row * m_cols + col;
            if (vis < 0 || vis >= m_visible.size()) continue;
            const int modelIdx = m_visible[vis];
            const core::SavedState& s = (*m_states)[modelIdx];
            const QRect r = tileRect(vis);
            if (!r.intersects(rect())) continue;

            QPixmap pix;
            bool cached = false;
            {
                QMutexLocker lk(&m_cacheMutex);
                if (const QPixmap* cachedPtr = m_pixCache.object(s.id)) {
                    pix = *cachedPtr;
                    cached = true;
                }
            }
            if (!cached) {
                pix = renderTilePlaceholder(s.name);
                requestLoad(s.id, s.name, s.previewImagePath);
            }

            const qreal fadeOp = cached ? m_fadeIn.value(s.id, 1.0) : 1.0;
            p.setOpacity(std::clamp(fadeOp, 0.0, 1.0));
            p.drawPixmap(r, pix);
            p.setOpacity(1.0);

            // Hover indicator: bright outline around the rounded tile edge.
            if (vis == m_hoverIndex) {
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(QColor("#3a3a3a"), 1));
                p.drawRoundedRect(QRectF(r.x() + 0.5, r.y() + 0.5,
                                         r.width() - 1.0, r.height() - 1.0),
                                  4, 4);
            }
        }
    }
}

void StatesGridView::resizeEvent(QResizeEvent*)
{
    recomputeLayout();
}

void StatesGridView::wheelEvent(QWheelEvent* e)
{
    const int maxScroll = std::max(0, m_totalH - height());
    const qreal delta = e->angleDelta().y();
    // Scale wheel delta to tile size so a notch moves ~1/3 of a tile row.
    m_scrollYTarget -= delta * 0.6;
    m_scrollYTarget = std::clamp(m_scrollYTarget, 0.0, static_cast<qreal>(maxScroll));
    e->accept();
}

void StatesGridView::mouseMoveEvent(QMouseEvent* e)
{
    // Hover index is the visible-list position (so we can hit-test rects).
    const QPoint p = e->position().toPoint();
    int hover = -1;
    for (int vis = 0; vis < m_visible.size(); ++vis) {
        if (tileRect(vis).contains(p)) {
            hover = vis;
            break;
        }
    }
    if (hover != m_hoverIndex) {
        m_hoverIndex = hover;
        if (hover >= 0)
            emit tileEntered(m_visible[hover]);
        else
            emit tileLeft();
        update();
    }
}

void StatesGridView::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_pressedLeft = true;
        m_pressPos = e->position().toPoint();
        m_pressedVis = -1;
        for (int vis = 0; vis < m_visible.size(); ++vis)
            if (tileRect(vis).contains(m_pressPos)) {
                m_pressedVis = vis;
                break;
            }
    }
    else if (e->button() == Qt::RightButton) {
        const int model = indexAtPoint(e->position().toPoint());
        if (model >= 0) emit tileContextMenuRequested(model, e->globalPosition().toPoint());
    }
    QWidget::mousePressEvent(e);
}

void StatesGridView::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(e);
        return;
    }
    const bool wasPressed = m_pressedLeft;
    m_pressedLeft = false;
    const int pressedVis = m_pressedVis;
    m_pressedVis = -1;
    if (!wasPressed) return;
    // Click iff release lands on the same tile pressed.
    const int releaseVis = [&]() {
        for (int vis = 0; vis < m_visible.size(); ++vis)
            if (tileRect(vis).contains(e->position().toPoint())) return vis;
        return -1;
    }();
    if (releaseVis >= 0 && releaseVis == pressedVis)
        emit tileClicked(m_visible[releaseVis]);
}

void StatesGridView::leaveEvent(QEvent*)
{
    if (m_hoverIndex != -1) {
        m_hoverIndex = -1;
        emit tileLeft();
        update();
    }
}

void StatesGridView::dragEnterEvent(QDragEnterEvent* e)
{
    if (!e->mimeData()->hasUrls()) {
        e->ignore();
        return;
    }
    for (const QUrl& u : e->mimeData()->urls())
        if (isImagePath(u.toLocalFile())) {
            e->acceptProposedAction();
            return;
        }
    e->ignore();
}

void StatesGridView::dragMoveEvent(QDragMoveEvent* e)
{
    if (indexAtPoint(e->position().toPoint()) >= 0)
        e->acceptProposedAction();
    else
        e->ignore();
}

void StatesGridView::dropEvent(QDropEvent* e)
{
    const int model = indexAtPoint(e->position().toPoint());
    if (model < 0) {
        e->ignore();
        return;
    }
    for (const QUrl& u : e->mimeData()->urls()) {
        const QString path = u.toLocalFile();
        if (isImagePath(path)) {
            emit tileImageDropped(model, path);
            e->acceptProposedAction();
            return;
        }
    }
    e->ignore();
}

void StatesGridView::hideEvent(QHideEvent* e)
{
    // No paints will land while we're hidden anyway; stopping the timer
    // saves the per-tick callback cost.
    m_animTimer->stop();
    QWidget::hideEvent(e);
}

void StatesGridView::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    // Unconditional start (not gated on pending fades) and an immediate
    // update() so the opacity-effect chain has a fresh dirty marker on the
    // first frame after re-show.
    m_animTimer->start();
    update();
}

} // namespace gui
