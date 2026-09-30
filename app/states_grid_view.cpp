#include <app/states_grid_view.h>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
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
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

// Same tile recipe as the entry view, letterboxed on a dark backdrop.
QImage composeTile(const QImage& source, const QString& name, int side, qreal gradientStart,
                   int gradientAlpha, const QColor& titleColor)
{
    constexpr int kRadius = 4;
    constexpr int kTitleHeight = 26;

    QImage out(side, side, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);

    QPainter painter(&out);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, side, side), kRadius, kRadius);
    painter.setClipPath(clip);

    painter.fillRect(QRect(0, 0, side, side), QColor(0x0a, 0x0a, 0x0a));

    if (!source.isNull()) {
        const QImage scaled =
            source.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        painter.drawImage(QPoint((side - scaled.width()) / 2, (side - scaled.height()) / 2),
                          scaled);
    }

    const qreal startY = side * gradientStart;
    QLinearGradient gradient(0, startY, 0, side);
    gradient.setColorAt(0.0, QColor(0, 0, 0, 0));
    gradient.setColorAt(1.0, QColor(0, 0, 0, gradientAlpha));
    painter.fillRect(QRectF(0, startY, side, side - startY), gradient);

    const QStringList families = QFontDatabase::applicationFontFamilies(0);
    QFont font = families.isEmpty() ? QGuiApplication::font() : QFont(families.first());
    font.setPointSize(13);
    font.setHintingPreference(QFont::PreferFullHinting);
    font.setStyleStrategy(QFont::PreferAntialias);

    painter.setFont(font);
    painter.setPen(titleColor);

    const QFontMetrics metrics(font);
    painter.drawText(QRect(8, side - kTitleHeight, side - 16, 22),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     metrics.elidedText(name, Qt::ElideRight, side - 16));

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QColor(0x1a, 0x1a, 0x1a));
    painter.drawRoundedRect(QRectF(0.5, 0.5, side - 1.0, side - 1.0), kRadius, kRadius);

    return out;
}

} // namespace

bool StatesGridView::isImagePath(const QString& path)
{
    static const QStringList suffixes = {u".jpg"_s, u".jpeg"_s, u".png"_s, u".webp"_s};
    const QString lower = path.toLower();
    for (const QString& suffix : suffixes)
        if (lower.endsWith(suffix)) return true;
    return false;
}

StatesGridView::StatesGridView(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);

    m_pixmapCache.setMaxCost(200);

    // Scroll easing and fades. Updates every tick on purpose: the parent's opacity
    // effect doesn't reliably repaint after a re-show otherwise. Runs only while
    // visible.
    m_animTimer = new QTimer(this);
    m_animTimer->setInterval(16);
    connect(m_animTimer, &QTimer::timeout, this, [this]() {
        const qreal remaining = m_scrollTarget - m_scrollActual;
        if (std::abs(remaining) > 0.5)
            m_scrollActual += remaining * 0.18;
        else if (m_scrollActual != m_scrollTarget)
            m_scrollActual = m_scrollTarget;

        constexpr qreal fadeStep = 16.0 / 200.0; // a 200ms fade in
        for (auto it = m_fadeIn.begin(); it != m_fadeIn.end(); ++it)
            if (it.value() < 1.0) it.value() = std::min(1.0, it.value() + fadeStep);

        update();
    });
}

void StatesGridView::setStates(const QList<SavedState>* states)
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

    // Replay the fade on filter changes; the cache is kept.
    if (m_states)
        for (int index : m_visible)
            m_fadeIn[(*m_states)[index].id] = 0.0;

    update();
}

void StatesGridView::setTileGradient(qreal start, int alpha)
{
    m_gradientStart = std::clamp(start, 0.0, 1.0);
    m_gradientAlpha = std::clamp(alpha, 0, 255);
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
    const QMutexLocker lock(&m_cacheMutex);
    m_pixmapCache.remove(id);
    m_fadeIn.remove(id);
}

void StatesGridView::clearTileCache()
{
    const QMutexLocker lock(&m_cacheMutex);
    m_pixmapCache.clear();
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

    for (int i = 0; i < int(m_states->size()); ++i)
        if (m_filter.isEmpty() || (*m_states)[i].name.contains(m_filter, Qt::CaseInsensitive))
            m_visible << i;
}

void StatesGridView::recomputeLayout()
{
    const int stride = m_tileSide + m_spacing;

    m_columns = std::max(1, (width() + m_spacing) / stride);
    m_offsetX =
        std::max(0, (width() - (m_columns * m_tileSide + (m_columns - 1) * m_spacing)) / 2);

    const int rows = int((m_visible.size() + m_columns - 1) / m_columns);
    m_totalHeight = rows * m_tileSide + std::max(0, rows - 1) * m_spacing + kPadV * 2;

    const int maxScroll = std::max(0, m_totalHeight - height());
    if (m_scrollTarget > maxScroll) m_scrollTarget = maxScroll;
    if (m_scrollActual > maxScroll) m_scrollActual = maxScroll;
}

QRect StatesGridView::tileRect(int visibleIndex) const
{
    const int stride = m_tileSide + m_spacing;
    const int column = visibleIndex % m_columns;
    const int row = visibleIndex / m_columns;

    return {m_offsetX + column * stride, kPadV + row * stride - int(m_scrollActual), m_tileSide,
            m_tileSide};
}

int StatesGridView::indexAtPoint(QPoint point) const
{
    if (m_columns <= 0) return -1;

    const int stride = m_tileSide + m_spacing;
    const int relativeX = point.x() - m_offsetX;
    const int relativeY = point.y() - kPadV + int(m_scrollActual);
    if (relativeX < 0 || relativeY < 0) return -1;

    const int column = relativeX / stride;
    const int row = relativeY / stride;
    if (column >= m_columns) return -1;

    // Gaps between tiles don't count.
    if (relativeX - column * stride >= m_tileSide) return -1;
    if (relativeY - row * stride >= m_tileSide) return -1;

    const int visible = row * m_columns + column;
    if (visible < 0 || visible >= m_visible.size()) return -1;
    return m_visible[visible];
}

QPixmap StatesGridView::renderTilePlaceholder(const QString& name) const
{
    return QPixmap::fromImage(composeTile(QImage(), name, m_tileSide, m_gradientStart,
                                          m_gradientAlpha, m_titleColor));
}

void StatesGridView::requestLoad(const QString& id, const QString& name,
                                 const QString& previewPath)
{
    {
        const QMutexLocker lock(&m_cacheMutex);
        if (m_pending.contains(id)) return;
        m_pending.insert(id);
    }

    const int generation = m_generation;
    const int side = m_tileSide;
    const qreal gradientStart = m_gradientStart;
    const int gradientAlpha = m_gradientAlpha;
    const QColor titleColor = m_titleColor;

    QThreadPool::globalInstance()->start([this, id, name, previewPath, generation, side,
                                          gradientStart, gradientAlpha, titleColor]() {
        QImage preview;
        if (!previewPath.isEmpty() && QFile::exists(previewPath)) preview.load(previewPath);

        QImage composed =
            composeTile(preview, name, side, gradientStart, gradientAlpha, titleColor);

        QMetaObject::invokeMethod(
            this,
            [this, id, generation, composed = std::move(composed)]() {
                if (generation != m_generation) {
                    const QMutexLocker lock(&m_cacheMutex);
                    m_pending.remove(id);
                    return;
                }
                {
                    const QMutexLocker lock(&m_cacheMutex);
                    m_pixmapCache.insert(id, new QPixmap(QPixmap::fromImage(composed)));
                    m_pending.remove(id);
                }
                m_fadeIn[id] = 0.0;
                update();
            },
            Qt::QueuedConnection);
    });
}

void StatesGridView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    if (!m_states || m_visible.isEmpty()) return;

    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    const int stride = std::max(1, m_tileSide + m_spacing);
    const int firstRow = std::max(0, (int(m_scrollActual) - kPadV) / stride);
    const int maxRow = int((m_visible.size() - 1) / std::max(1, m_columns));
    const int lastRow =
        std::min(maxRow, (int(m_scrollActual) + height() - kPadV) / stride + 1);

    for (int row = firstRow; row <= lastRow; ++row) {
        for (int column = 0; column < m_columns; ++column) {
            const int visible = row * m_columns + column;
            if (visible < 0 || visible >= m_visible.size()) continue;

            const SavedState& state = (*m_states)[m_visible[visible]];
            const QRect box = tileRect(visible);
            if (!box.intersects(rect())) continue;

            QPixmap tile;
            bool cached = false;
            {
                const QMutexLocker lock(&m_cacheMutex);
                if (const QPixmap* hit = m_pixmapCache.object(state.id)) {
                    tile = *hit;
                    cached = true;
                }
            }
            if (!cached) {
                tile = renderTilePlaceholder(state.name);
                requestLoad(state.id, state.name, state.previewImagePath);
            }

            painter.setOpacity(
                std::clamp(cached ? m_fadeIn.value(state.id, 1.0) : 1.0, 0.0, 1.0));
            painter.drawPixmap(box, tile);
            painter.setOpacity(1.0);

            if (visible != m_hoverIndex) continue;
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(0x3a, 0x3a, 0x3a), 1));
            painter.drawRoundedRect(
                QRectF(box.x() + 0.5, box.y() + 0.5, box.width() - 1.0, box.height() - 1.0), 4,
                4);
        }
    }
}

void StatesGridView::resizeEvent(QResizeEvent*)
{
    recomputeLayout();
}

void StatesGridView::wheelEvent(QWheelEvent* event)
{
    const int maxScroll = std::max(0, m_totalHeight - height());

    // Scaled so a notch moves about a third of a tile row.
    m_scrollTarget -= event->angleDelta().y() * 0.6;
    m_scrollTarget = std::clamp(m_scrollTarget, 0.0, qreal(maxScroll));
    event->accept();
}

void StatesGridView::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint point = event->position().toPoint();

    int hover = -1;
    for (int visible = 0; visible < int(m_visible.size()); ++visible) {
        if (!tileRect(visible).contains(point)) continue;
        hover = visible;
        break;
    }
    if (hover == m_hoverIndex) return;

    m_hoverIndex = hover;
    if (hover >= 0)
        emit tileEntered(m_visible[hover]);
    else
        emit tileLeft();
    update();
}

void StatesGridView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressedLeft = true;
        m_pressPos = event->position().toPoint();
        m_pressedVisible = -1;

        for (int visible = 0; visible < int(m_visible.size()); ++visible) {
            if (!tileRect(visible).contains(m_pressPos)) continue;
            m_pressedVisible = visible;
            break;
        }
    } else if (event->button() == Qt::RightButton) {
        const int model = indexAtPoint(event->position().toPoint());
        if (model >= 0)
            emit tileContextMenuRequested(model, event->globalPosition().toPoint());
    }
    QWidget::mousePressEvent(event);
}

void StatesGridView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    const bool wasPressed = m_pressedLeft;
    const int pressedVisible = m_pressedVisible;
    m_pressedLeft = false;
    m_pressedVisible = -1;
    if (!wasPressed) return;

    // A click needs press and release on the same tile.
    const QPoint point = event->position().toPoint();
    for (int visible = 0; visible < int(m_visible.size()); ++visible) {
        if (!tileRect(visible).contains(point)) continue;
        if (visible == pressedVisible) emit tileClicked(m_visible[visible]);
        return;
    }
}

void StatesGridView::leaveEvent(QEvent*)
{
    if (m_hoverIndex == -1) return;
    m_hoverIndex = -1;
    emit tileLeft();
    update();
}

void StatesGridView::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }
    for (const QUrl& url : event->mimeData()->urls()) {
        if (!isImagePath(url.toLocalFile())) continue;
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void StatesGridView::dragMoveEvent(QDragMoveEvent* event)
{
    if (indexAtPoint(event->position().toPoint()) >= 0)
        event->acceptProposedAction();
    else
        event->ignore();
}

void StatesGridView::dropEvent(QDropEvent* event)
{
    const int model = indexAtPoint(event->position().toPoint());
    if (model < 0) {
        event->ignore();
        return;
    }

    for (const QUrl& url : event->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (!isImagePath(path)) continue;
        emit tileImageDropped(model, path);
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void StatesGridView::hideEvent(QHideEvent* event)
{
    m_animTimer->stop();
    QWidget::hideEvent(event);
}

void StatesGridView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    // Immediate update so the opacity effect repaints after a re-show.
    m_animTimer->start();
    update();
}

} // namespace tc
