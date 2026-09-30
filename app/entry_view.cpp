#include <app/entry_view.h>
#include <app/entry_nav_panel.h>
#include <core/entry_store.h>
#include <QApplication>
#include <QCursor>
#include <QDateTime>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QAction>
#include <QMenu>
#include <QMouseEvent>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>
#include <QThreadPool>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

constexpr int kFrameMs = 16;
constexpr qreal kScrollSmoothing = 0.12;
constexpr qreal kFlingFriction = 0.94;
constexpr qreal kFlingStop = 20.0;
constexpr int kPreloadRows = 3;

qreal easeInOutSine(qreal t)
{
    return -(std::cos(M_PI * t) - 1.0) / 2.0;
}

} // namespace

EntryView::EntryView(const EntryStore& store, QWidget* parent) : QWidget(parent), m_store(&store)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);

    m_tiles.setMaxCost(200);

    m_tileFont = QApplication::font();
    m_tileFont.setPointSize(13);
    m_tileFont.setHintingPreference(QFont::PreferFullHinting);
    m_tileFont.setStyleStrategy(QFont::PreferAntialias);

    m_placeholder = QPixmap(kTileW, kTileH);
    m_placeholder.fill(Qt::transparent);

    // For entries without a usable image.
    QImage source(u":/img/placeholder.png"_s);
    if (source.isNull()) {
        source = QImage(kTileW, kTileH, QImage::Format_ARGB32_Premultiplied);
        source.fill(QColor(28, 28, 28));
    }
    const QImage scaled =
        source.scaled(kTileW, kTileH, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    m_emptyTile = QImage(kTileW, kTileH, QImage::Format_ARGB32_Premultiplied);
    m_emptyTile.fill(Qt::transparent);
    {
        QPainter painter(&m_emptyTile);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage((kTileW - scaled.width()) / 2, (kTileH - scaled.height()) / 2, scaled);
    }

    // One timer drives scrolling, fling decay, load fades and hover easing.
    m_animTimer = new QTimer(this);
    m_animTimer->setInterval(kFrameMs);
    connect(m_animTimer, &QTimer::timeout, this, &EntryView::onAnimationTick);

    m_navPanel = new EntryNavPanel(this);
    m_navPanel->onEntryClicked = [this](int index) {
        if (index < 0 || index >= m_uuids.size()) return;
        setSelected(m_uuids[index]);
        scrollToIndex(index);
        emit entryClicked(m_uuids[index]);
    };
    repositionNav();
}

void EntryView::setActiveEntries(const QSet<QString>& uuids)
{
    if (m_activeUuids == uuids) return;
    m_activeUuids = uuids;
    rebuildNavPanel();
    update();
}

void EntryView::rebuildNavPanel()
{
    if (!m_navPanel) return;

    QList<QPair<QString, int>> items;
    for (int i = 0; i < int(m_uuids.size()); ++i) {
        if (!m_activeUuids.contains(m_uuids[i])) continue;
        const Entry* entry = m_store->find(m_uuids[i]);
        const QString title = (entry && !entry->title.isEmpty()) ? entry->title
                                                                 : u"(untitled)"_s;
        items.append({title, i});
    }
    m_navPanel->setEntries(items);
}

void EntryView::repositionNav()
{
    if (!m_navPanel) return;
    constexpr int margin = 8;
    m_navPanel->move(width() - m_navPanel->width() - margin, margin - 2);
    m_navPanel->raise();
}

void EntryView::setEntries(const QStringList& uuids)
{
    m_uuids = uuids;
    m_anims.clear();
    m_hoverIndex = -1;
    m_scrollTarget = 0.0;
    m_scrollActual = 0.0;
    m_flingVelocity = 0.0;
    recomputeLayout();
    rebuildNavPanel();
    update();
}

void EntryView::setSelected(const QString& uuid)
{
    if (m_selected == uuid) return;
    m_selected = uuid;
    update();
}

QString EntryView::selected() const
{
    return m_selected;
}

void EntryView::setTileGradient(qreal start, int alpha)
{
    m_gradientStart = start;
    m_gradientAlpha = alpha;
    QMutexLocker lock(&m_tileMutex);
    m_tiles.clear();
}

void EntryView::setTileTitleColor(const QColor& colour)
{
    m_titleColour = colour;
    QMutexLocker lock(&m_tileMutex);
    m_tiles.clear();
}

void EntryView::recomputeLayout()
{
    const int strideX = kTileW + kSpacing;
    const int strideY = kTileH + kSpacing;

    m_cols = std::max(1, (width() + kSpacing) / strideX);
    m_offsetX = std::max(0, (width() - (m_cols * kTileW + (m_cols - 1) * kSpacing)) / 2);

    const int rows = int((m_uuids.size() + m_cols - 1) / m_cols);
    m_totalH = rows * kTileH + std::max(0, rows - 1) * kSpacing + kPadV * 2;

    // Big enough that eviction never hits a visible tile.
    constexpr int slackRows = 3;
    const int visibleRows = (height() + strideY - 1) / strideY + 1;
    m_tiles.setMaxCost(std::max(200, m_cols * (visibleRows + kPreloadRows + slackRows)));
}

QRect EntryView::tileRect(int index) const
{
    const int col = index % m_cols;
    const int row = index / m_cols;
    return QRect(m_offsetX + col * (kTileW + kSpacing),
                 kPadV + row * (kTileH + kSpacing) - int(m_scrollActual), kTileW, kTileH);
}

int EntryView::indexAt(QPoint pos) const
{
    const int col = (pos.x() - m_offsetX) / (kTileW + kSpacing);
    const int row = int((pos.y() + m_scrollActual - kPadV) / (kTileH + kSpacing));
    if (col < 0 || col >= m_cols || row < 0) return -1;

    const int index = row * m_cols + col;
    if (index < 0 || index >= m_uuids.size()) return -1;
    return tileRect(index).contains(pos) ? index : -1;
}

int EntryView::indexOf(const QString& uuid) const
{
    return int(m_uuids.indexOf(uuid));
}

void EntryView::scrollToUuid(const QString& uuid)
{
    const int index = indexOf(uuid);
    if (index >= 0) scrollToIndex(index);
}

void EntryView::scrollToIndex(int index)
{
    if (index < 0 || index >= m_uuids.size()) return;

    m_flingVelocity = 0.0;

    const int row = index / m_cols;
    const qreal top = kPadV + row * qreal(kTileH + kSpacing);
    const int maxScroll = std::max(0, m_totalH - height());
    const qreal target =
        std::clamp(top - (height() - kTileH) / 2.0, 0.0, qreal(maxScroll));

    // Jumps beyond one viewport snap, so we don't bake tiles that fly past.
    if (std::abs(target - m_scrollActual) > height()) {
        m_scrollActual = target;
        m_scrollTarget = target;
        update();
        return;
    }

    m_scrollTarget = target;
    if (!m_animTimer->isActive()) m_animTimer->start();
}

QImage EntryView::bakeTile(const QImage& source, const QString& title) const
{
    QImage out(kTileW, kTileH, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);

    QPainter painter(&out);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, kTileW, kTileH), kRadius, kRadius);
    painter.setClipPath(clip);
    painter.drawImage(QRect(0, 0, kTileW, kTileH), source);

    const qreal gradientTop = kTileH * m_gradientStart;
    QLinearGradient gradient(0, gradientTop, 0, kTileH);
    gradient.setColorAt(0.0, QColor(0, 0, 0, 0));
    gradient.setColorAt(1.0, QColor(0, 0, 0, m_gradientAlpha));
    painter.fillRect(QRectF(0, gradientTop, kTileW, kTileH - gradientTop), gradient);

    painter.setFont(m_tileFont);
    painter.setPen(m_titleColour);

    const QFontMetrics metrics(m_tileFont);
    painter.drawText(QRect(8, kTileH - 26, kTileW - 16, 22), Qt::AlignLeft | Qt::AlignVCenter,
                     metrics.elidedText(title, Qt::ElideRight, kTileW - 16));

    return out;
}

void EntryView::requestBake(int index)
{
    if (index < 0 || index >= m_uuids.size()) return;
    const QString uuid = m_uuids[index];

    {
        QMutexLocker lock(&m_tileMutex);
        if (m_baking.contains(uuid)) return;
        m_baking.insert(uuid);
    }

    const Entry* entry = m_store->find(uuid);
    const QString title = entry ? entry->title : QString();
    const QString path = (entry && !entry->images.isEmpty())
        ? m_store->folderFor(uuid) + u"/"_s + entry->images[0].fileName
        : QString();

    const auto publish = [this, uuid, index](QImage composed) {
        {
            QMutexLocker lock(&m_tileMutex);
            m_tiles.insert(uuid, new QPixmap(QPixmap::fromImage(composed)));
            m_baking.remove(uuid);
        }
        m_anims[index].fade = 0.0;
        if (!m_animTimer->isActive()) m_animTimer->start();
        update();
    };

    if (path.isEmpty()) {
        publish(bakeTile(m_emptyTile, title));
        return;
    }

    QThreadPool::globalInstance()->start([this, path, title, publish]() {
        QImage image(path);
        // A missing file still gets a tile, or paint would re-request it forever.
        const QImage scaled = image.isNull()
            ? m_emptyTile
            : image.scaled(QSize(kTileW, kTileH), Qt::KeepAspectRatioByExpanding,
                           Qt::SmoothTransformation);
        QImage composed = bakeTile(scaled, title);

        QMetaObject::invokeMethod(
            this, [publish, composed = std::move(composed)]() { publish(composed); },
            Qt::QueuedConnection);
    });
}

void EntryView::onAnimationTick()
{
    bool active = false;
    bool scrolled = false;

    // No fling while the button is down; the drag tracks the cursor.
    if (m_flingVelocity != 0.0 && !m_pressed) {
        const int maxScroll = std::max(0, m_totalH - height());
        const qreal next = m_scrollActual + m_flingVelocity * (kFrameMs / 1000.0);
        const qreal clamped = std::clamp(next, 0.0, qreal(maxScroll));
        if (clamped != next) m_flingVelocity = 0.0;

        m_scrollActual = clamped;
        m_scrollTarget = clamped;
        m_flingVelocity *= kFlingFriction;
        if (std::abs(m_flingVelocity) < kFlingStop) m_flingVelocity = 0.0;
        active = true;
        scrolled = true;
    }

    const qreal remaining = m_scrollTarget - m_scrollActual;
    if (std::abs(remaining) > 0.5) {
        m_scrollActual += remaining * kScrollSmoothing;
        active = true;
        scrolled = true;
    }
    else {
        m_scrollActual = m_scrollTarget;
    }

    if (scrolled && underMouse()) {
        const QPoint local = mapFromGlobal(QCursor::pos());
        if (rect().contains(local)) m_hoverIndex = indexAt(local);
    }

    constexpr qreal fadeStep = kFrameMs / 250.0;
    constexpr qreal hoverStep = kFrameMs / 200.0;

    for (auto it = m_anims.begin(); it != m_anims.end(); ++it) {
        TileAnim& anim = it.value();

        if (anim.fade < 1.0) {
            anim.fade = std::min(1.0, anim.fade + fadeStep);
            active = true;
        }

        const qreal target = (it.key() == m_hoverIndex) ? 1.0 : 0.0;
        const qreal diff = target - anim.hover;
        if (std::abs(diff) > 0.001) {
            anim.hover = std::clamp(anim.hover + (diff > 0 ? hoverStep : -hoverStep), 0.0, 1.0);
            active = true;
        }
    }

    if (active)
        update();
    else
        m_animTimer->stop();
}

void EntryView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0x0d, 0x0d, 0x0d));
    painter.setRenderHints(QPainter::SmoothPixmapTransform | QPainter::Antialiasing);

    if (m_uuids.isEmpty() || m_cols == 0) return;

    const int strideY = kTileH + kSpacing;
    const int startRow = std::max(0, int((m_scrollActual - kPadV) / strideY));
    const int endRow = int((m_scrollActual + height() - kPadV) / strideY) + 1;
    const int startIndex = startRow * m_cols;
    const int endIndex = std::min((endRow + 1) * m_cols, int(m_uuids.size()));

    for (int i = startIndex; i < endIndex; ++i) {
        const QRect r = tileRect(i);
        if (!r.intersects(rect())) continue;

        QPixmap tile;
        {
            QMutexLocker lock(&m_tileMutex);
            if (const QPixmap* cached = m_tiles.object(m_uuids[i])) tile = *cached;
        }

        if (tile.isNull()) {
            painter.drawPixmap(r.topLeft(), m_placeholder);
            requestBake(i);
            continue;
        }

        // New tiles fade in, cached or not, so a new query fades the whole grid.
        auto anim_it = m_anims.find(i);
        if (anim_it == m_anims.end()) {
            anim_it = m_anims.insert(i, TileAnim{0.0, 0.0});
            if (!m_animTimer->isActive()) m_animTimer->start();
        }
        const TileAnim& anim = *anim_it;
        const qreal hover = easeInOutSine(anim.hover);
        const qreal scale = 1.0 - 0.03 * hover;

        painter.save();
        painter.setOpacity(anim.fade * (1.0 - 0.25 * hover));
        painter.translate(r.center());
        painter.scale(scale, scale);
        painter.translate(-r.center());
        painter.drawPixmap(r.topLeft(), tile);

        // Pushed to the composer.
        if (m_activeUuids.contains(m_uuids[i])) {
            painter.setOpacity(1.0);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(74, 160, 74), 2.5));

            QPainterPath ring;
            ring.addRoundedRect(QRectF(r).adjusted(1.25, 1.25, -1.25, -1.25), kRadius, kRadius);
            painter.drawPath(ring);
        }

        // LoRA: corner dot, or an inner ring when active.
        {
            const Entry* entry = m_store->find(m_uuids[i]);
            const bool hasLora = entry && entry->lora.has_value();
            const bool loraActive = hasLora && m_activeLoraUuids.contains(m_uuids[i]);

            if (loraActive) {
                painter.setOpacity(1.0);
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(QColor(220, 150, 30), 2.5));

                QPainterPath ring;
                ring.addRoundedRect(QRectF(r).adjusted(4.0, 4.0, -4.0, -4.0), kRadius - 2.0,
                                    kRadius - 2.0);
                painter.drawPath(ring);
            } else if (hasLora) {
                painter.setOpacity(0.85);
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(220, 150, 30));
                painter.drawRoundedRect(QRectF(r.right() - 17.0, r.top() + 9.0, 10.0, 10.0), 3.0,
                                        3.0);
            }
        }

        if (m_uuids[i] == m_selected) {
            painter.setOpacity(1.0);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(255, 255, 255, 90), 4.0));
            QPainterPath outer;
            outer.addRoundedRect(QRectF(r).adjusted(-2.0, -2.0, 2.0, 2.0), kRadius + 2.0,
                                 kRadius + 2.0);
            painter.drawPath(outer);

            painter.setPen(QPen(QColor(255, 255, 255, 220), 1.5));
            QPainterPath inner;
            inner.addRoundedRect(QRectF(r).adjusted(-0.5, -0.5, 0.5, 0.5), kRadius + 0.5,
                                 kRadius + 0.5);
            painter.drawPath(inner);
        }

        painter.restore();
    }

    const int preloadEnd = std::min(endIndex + m_cols * kPreloadRows, int(m_uuids.size()));
    for (int i = endIndex; i < preloadEnd; ++i) {
        bool needed = false;
        {
            QMutexLocker lock(&m_tileMutex);
            needed = !m_tiles.contains(m_uuids[i]) && !m_baking.contains(m_uuids[i]);
        }
        if (needed) requestBake(i);
    }
}

void EntryView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    recomputeLayout();
    repositionNav();
}

void EntryView::wheelEvent(QWheelEvent* event)
{
    const int maxScroll = std::max(0, m_totalH - height());
    m_flingVelocity = 0.0;
    m_scrollTarget = std::clamp(m_scrollTarget - event->angleDelta().y(), 0.0, qreal(maxScroll));
    if (!m_animTimer->isActive()) m_animTimer->start();
    event->accept();
}

void EntryView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton) {
        const int index = indexAt(event->pos());
        if (index >= 0) showTileMenu(index, event->globalPosition().toPoint());
        return;
    }
    if (event->button() != Qt::LeftButton) return;

    m_pressed = true;
    m_dragging = false;
    m_pressPos = event->pos();
    m_pressScroll = m_scrollTarget;
    m_flingVelocity = 0.0;
    m_lastMovePos = event->pos();
    m_lastMoveTime = QDateTime::currentMSecsSinceEpoch();
    setFocus(Qt::MouseFocusReason);
}

void EntryView::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_pressed) {
        const int hover = indexAt(event->pos());
        if (hover != m_hoverIndex) {
            m_hoverIndex = hover;
            if (!m_animTimer->isActive()) m_animTimer->start();
        }
        return;
    }

    const int travelled = (event->pos() - m_pressPos).manhattanLength();
    if (!m_dragging && travelled >= QApplication::startDragDistance()) m_dragging = true;
    if (!m_dragging) return;

    const int maxScroll = std::max(0, m_totalH - height());
    m_scrollTarget = std::clamp(m_pressScroll - (event->pos().y() - m_pressPos.y()), 0.0,
                                qreal(maxScroll));
    m_scrollActual = m_scrollTarget;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 elapsed = now - m_lastMoveTime;
    if (elapsed > 0) {
        const qreal dy = m_lastMovePos.y() - event->pos().y();
        m_flingVelocity = dy * 1000.0 / qreal(elapsed);
        m_lastMoveTime = now;
        m_lastMovePos = event->pos();
    }
    update();
}

void EntryView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;

    const bool wasDragging = m_dragging;
    m_pressed = false;
    m_dragging = false;

    if (wasDragging) {
        if (m_flingVelocity != 0.0 && !m_animTimer->isActive()) m_animTimer->start();
        return;
    }

    m_flingVelocity = 0.0;
    const int index = indexAt(event->pos());
    if (index < 0) return;

    setSelected(m_uuids[index]);
    scrollToIndex(index);
    emit entryClicked(m_uuids[index]);
    emit entryClickedByPointer(m_uuids[index]);
}

void EntryView::setActiveLoras(const QSet<QString>& uuids)
{
    if (m_activeLoraUuids == uuids) return;
    m_activeLoraUuids = uuids;
    update();
}

void EntryView::showTileMenu(int index, const QPoint& globalPos)
{
    const Entry* entry = m_store->find(m_uuids[index]);
    if (!entry) return;

    QMenu menu(this);

    QAction* composerAction = nullptr;
    if (!entry->images.isEmpty()) {
        const bool pushed = m_activeUuids.contains(entry->uuid);
        composerAction =
            menu.addAction(pushed ? u"Remove from Composer"_s : u"Add to Composer"_s);
    }

    QAction* loraAction = nullptr;
    if (entry->lora.has_value()) {
        if (!menu.isEmpty()) menu.addSeparator();
        const bool active = m_activeLoraUuids.contains(entry->uuid);
        loraAction = menu.addAction(active ? u"Deactivate LoRA"_s : u"Activate LoRA"_s);
    }

    if (menu.isEmpty()) return;

    QAction* chosen = menu.exec(globalPos);
    if (!chosen) return;

    if (chosen == composerAction)
        emit entryActivated(entry->uuid);
    else if (chosen == loraAction)
        emit loraToggled(entry->uuid);
}

void EntryView::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    if (m_hoverIndex == -1) return;
    m_hoverIndex = -1;
    if (!m_animTimer->isActive()) m_animTimer->start();
}

void EntryView::keyPressEvent(QKeyEvent* event)
{
    if (m_uuids.isEmpty()) {
        QWidget::keyPressEvent(event);
        return;
    }

    const int last = int(m_uuids.size()) - 1;
    const int current = m_selected.isEmpty() ? 0 : indexOf(m_selected);

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (current >= 0) emit entryActivated(m_uuids[current]);
        event->accept();
        return;
    }

    const int rowsPerPage = std::max(1, height() / (kTileH + kSpacing));

    int next = current;
    switch (event->key()) {
    case Qt::Key_Left:
        next = current - 1;
        break;
    case Qt::Key_Right:
        next = current + 1;
        break;
    case Qt::Key_Up:
        next = current - m_cols;
        break;
    case Qt::Key_Down:
        next = current + m_cols;
        break;
    case Qt::Key_Home:
        next = 0;
        break;
    case Qt::Key_End:
        next = last;
        break;
    case Qt::Key_PageUp:
        next = current - rowsPerPage * m_cols;
        break;
    case Qt::Key_PageDown:
        next = current + rowsPerPage * m_cols;
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }

    // Clamped rather than wrapped, so the last partial row stays reachable.
    next = std::clamp(next, 0, last);
    setSelected(m_uuids[next]);
    scrollToIndex(next);
    emit entryClicked(m_uuids[next]);
    event->accept();
}

bool EntryView::focusNextPrevChild(bool)
{
    emit focusFilterRequested();
    return true;
}

} // namespace tc
