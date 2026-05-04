#include <gui/tileview/entryview.h>
#include <utils/appconfig.h>
#include <utils/qutils.h>
#include <QThreadPool>
#include <QFontDatabase>
#include <QMenu>
#include <QCursor>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QPropertyAnimation>
#include <functional>
#include <cmath>
#include <algorithm>

using namespace core;
using namespace utils;

// ── EntryNavPanel ─────────────────────────────────────────────────────────────
// Floating top-right widget: handle that expands on hover to show a scrollable
// list of the current entries. Clicking one smoothly scrolls to it in the view.

namespace {

class EntryNavPanel : public QWidget {
public:
    std::function<void(int)> onEntryClicked;

    explicit EntryNavPanel(QWidget* parent = nullptr) : QWidget(parent)
    {
        setAttribute(Qt::WA_StyledBackground, true);
        setObjectName("EntryNavPanel");
        setFixedWidth(160);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        m_handle = new QLabel("☰  Active Entries", this);
        m_handle->setObjectName("EntryNavHandle");
        m_handle->setFixedHeight(26);
        m_handle->setAlignment(Qt::AlignCenter);
        root->addWidget(m_handle);

        m_listFrame = new QWidget(this);
        m_listFrame->setObjectName("EntryNavList");
        m_listLayout = new QVBoxLayout(m_listFrame);
        m_listLayout->setContentsMargins(0, 2, 0, 2);
        m_listLayout->setSpacing(0);

        root->addWidget(m_listFrame);
        m_listFrame->setMaximumHeight(0);

        m_anim = new QPropertyAnimation(m_listFrame, "maximumHeight", this);
        m_anim->setEasingCurve(QEasingCurve::InOutQuad);
        m_anim->setDuration(160);
        connect(m_anim, &QPropertyAnimation::valueChanged, m_listFrame,
                [this](const QVariant&) { adjustSize(); });
    }

    // items: (display title, index into m_entries for scroll target)
    void updateEntries(const QList<QPair<QString, int>>& items)
    {
        while (m_listLayout->count()) {
            auto* item = m_listLayout->takeAt(0);
            if (auto* w = item->widget()) w->deleteLater();
            delete item;
        }

        for (const auto& [title, entryIdx] : items) {
            auto* btn = new QPushButton(title, m_listFrame);
            btn->setObjectName("EntryNavBtn");
            btn->setFixedHeight(24);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setFlat(true);
            connect(btn, &QPushButton::clicked, btn, [this, entryIdx]() {
                if (onEntryClicked) onEntryClicked(entryIdx);
            });
            m_listLayout->addWidget(btn);
        }

        m_fullHeight = items.isEmpty() ? 0 : std::min((int)items.size() * 24 + 4, 1000);
        if (m_listFrame->maximumHeight() > 0) m_listFrame->setMaximumHeight(m_fullHeight);

        adjustSize();
    }

protected:
    void enterEvent(QEnterEvent*) override
    {
        if (m_fullHeight == 0) return;
        m_anim->stop();
        m_anim->setStartValue(m_listFrame->maximumHeight());
        m_anim->setEndValue(m_fullHeight);
        m_anim->start();
    }

    void leaveEvent(QEvent*) override
    {
        m_anim->stop();
        m_anim->setStartValue(m_listFrame->maximumHeight());
        m_anim->setEndValue(0);
        m_anim->start();
    }

private:
    QLabel* m_handle;
    QWidget* m_listFrame;
    QVBoxLayout* m_listLayout;
    QPropertyAnimation* m_anim;
    int m_fullHeight = 0;
};

} // anonymous namespace

namespace gui {

// ── easing ────────────────────────────────────────────────────────────────────

static qreal inOutSine(qreal t)
{
    return -(std::cos(M_PI * t) - 1.0) / 2.0;
}

// ── ctor ──────────────────────────────────────────────────────────────────────

EntryView::EntryView(EntryModel* model, QWidget* parent) : QWidget(parent), m_model(model)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);

    // Cap at 200 tiles (~55 MB at 234 * 300 * 4 bytes per tile).
    // Tuned for typical use: viewport plus 3-row preload fits well under
    // the cap, and scrolling through more entries than this evicts oldest.
    m_pixCache.setMaxCost(200);

    // Transparent in-flight placeholder - keeps the initial paint clean
    // (no flash of the placeholder image while real images stream in).
    m_placeholder = QPixmap(TileW, TileH);
    m_placeholder.fill(Qt::transparent);

    // Pre-scaled resource image used by makeTileImage when an entry has
    // no image (or its image file is missing). Center-cropped to tile size.
    {
        QImage src(":/img/placeholder.png");
        if (src.isNull()) {
            src = QImage(TileW, TileH, QImage::Format_ARGB32_Premultiplied);
            src.fill(QColor(28, 28, 28));
        }
        const QImage scaled =
            src.scaled(TileW, TileH, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        m_emptyTileBg = QImage(TileW, TileH, QImage::Format_ARGB32_Premultiplied);
        m_emptyTileBg.fill(Qt::transparent);
        QPainter p(&m_emptyTileBg);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const int dx = (TileW - scaled.width()) / 2;
        const int dy = (TileH - scaled.height()) / 2;
        p.drawImage(dx, dy, scaled);
    }

    // Single shared animation timer (~60 fps).
    // Drives both load fade-in and hover scale/opacity transitions.
    m_animTimer = new QTimer(this);
    m_animTimer->setInterval(16);
    connect(m_animTimer, &QTimer::timeout, this, [this]() {
        bool anyActive = false;

        const qreal scrollDiff = m_scrollYTarget - m_scrollYActual;
        if (std::abs(scrollDiff) > 0.5) {
            m_scrollYActual += scrollDiff * 0.12; // 0.12 = scroll smoothing factor
            anyActive = true;
        }
        else {
            m_scrollYActual = m_scrollYTarget; // snap when close enough
        }

        // 500 ms fade-in, 400 ms hover transition
        const qreal fadeStep = 16.0 / 250.0;
        const qreal hoverStep = 16.0 / 200.0;

        for (auto it = m_anims.begin(); it != m_anims.end(); ++it) {
            TileAnim& a = it.value();

            // Load fade-in
            if (a.fadeOpacity < 1.0) {
                a.fadeOpacity = std::min(1.0, a.fadeOpacity + fadeStep);
                anyActive = true;
            }

            // Hover transition - advance toward target (0 or 1)
            const qreal targetHover = (it.key() == m_hoverIndex) ? 1.0 : 0.0;
            const qreal diff = targetHover - a.hoverT;
            if (std::abs(diff) > 0.001) {
                a.hoverT += (diff > 0 ? 1.0 : -1.0) * hoverStep;
                a.hoverT = std::clamp(a.hoverT, 0.0, 1.0);
                anyActive = true;
            }
        }

        if (anyActive)
            update();
        else
            m_animTimer->stop();
    });

    // Floating entry nav panel
    auto* navPanel = new EntryNavPanel(this);
    m_navPanel = navPanel;
    navPanel->onEntryClicked = [this](int idx) { scrollToEntry(idx); };
    repositionNav();
}

// ── public ────────────────────────────────────────────────────────────────────

void EntryView::setActiveGroups(const QMap<int, QList<int>>& groups)
{
    m_activeEntryIds.clear();
    for (auto it = groups.begin(); it != groups.end(); ++it)
        m_activeEntryIds.insert(it.key());
    rebuildNavPanel();
    update();
}

void EntryView::query(const QString& q)
{
    ++m_generation;
    m_entries = m_model->filter(q);
    m_scrollYTarget = 0.0;
    m_scrollYActual = 0.0;
    m_hoverIndex = -1;

    {
        QMutexLocker lk(&m_cacheMutex);
        m_pixCache.clear();
        m_pending.clear();
    }

    m_anims.clear();
    m_animTimer->stop();

    recomputeLayout();

    // If an entry is selected and still in the result set, restore its scroll position.
    if (m_selectedEntryId >= 0) {
        for (int i = 0; i < m_entries.size(); ++i) {
            if (m_entries[i]->id == m_selectedEntryId) {
                const int row = i / std::max(1, m_cols);
                const qreal tileTop = PadV + row * static_cast<qreal>(TileH + Spacing);
                const qreal centered = tileTop - (height() - TileH) / 2.0;
                const int maxScroll = std::max(0, m_totalH - height());
                const qreal pos = std::clamp(centered, 0.0, static_cast<qreal>(maxScroll));
                m_scrollYTarget = pos;
                m_scrollYActual = pos;
                break;
            }
        }
    }

    rebuildNavPanel();
    update();
}

// ── layout ────────────────────────────────────────────────────────────────────

void EntryView::recomputeLayout()
{
    const int strideX = TileW + Spacing;
    const int strideY = TileH + Spacing;
    m_cols = std::max(1, (width() + Spacing) / strideX);
    m_offsetX = std::max(0, (width() - (m_cols * TileW + (m_cols - 1) * Spacing)) / 2);
    int rows = (m_entries.size() + m_cols - 1) / m_cols;
    m_totalH = rows * TileH + std::max(0, rows - 1) * Spacing + PadV * 2;

    // Bound the cache to (visible + preload + scroll slack) so eviction never
    // targets an on-screen tile. Floor at 200 (~55 MB) - preserves the cap on
    // normal monitors; only grows on viewports big enough to need it.
    constexpr int preloadRows = 3; // matches the preload window in paintEvent
    constexpr int slackRows = 3;   // covers tiles scrolling in/out mid-frame
    const int visibleRows = (height() + strideY - 1) / strideY + 1;
    const int workingSet = m_cols * (visibleRows + preloadRows + slackRows);
    m_pixCache.setMaxCost(std::max(200, workingSet));
}

QRect EntryView::tileRect(int index) const
{
    const int strideX = TileW + Spacing;
    const int strideY = TileH + Spacing;
    const int col = index % m_cols;
    const int row = index / m_cols;
    return QRect(m_offsetX + col * strideX, PadV + row * strideY - m_scrollYActual, TileW, TileH);
}

int EntryView::indexAt(QPoint p) const
{
    const int strideX = TileW + Spacing;
    const int strideY = TileH + Spacing;
    const int col = (p.x() - m_offsetX) / strideX;
    const int row = (p.y() + m_scrollYActual - PadV) / strideY;

    if (col < 0 || col >= m_cols || row < 0) return -1;
    const int idx = row * m_cols + col;
    if (idx < 0 || idx >= (int)m_entries.size()) return -1;

    // Reject clicks/hovers that land in the spacing gap
    return tileRect(idx).contains(p) ? idx : -1;
}

// ── events ────────────────────────────────────────────────────────────────────

void EntryView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    recomputeLayout();
    m_scrollYTarget = 0.0;
    m_scrollYActual = 0.0;
    repositionNav();
    update();
}

void EntryView::wheelEvent(QWheelEvent* event)
{
    const int delta = event->angleDelta().y();
    const int maxScroll = std::max(0, m_totalH - height());

    m_scrollYTarget -= delta / 120.0 * (TileH / 3.0);
    m_scrollYTarget = std::clamp(m_scrollYTarget, 0.0, (qreal)maxScroll);

    if (!m_animTimer->isActive()) m_animTimer->start();
    event->accept();
}

void EntryView::mouseMoveEvent(QMouseEvent* event)
{
    const int newHover = indexAt(event->pos());
    if (newHover != m_hoverIndex) {
        m_hoverIndex = newHover;
        // Ensure both the tile we're entering and the one we're leaving animate
        if (!m_animTimer->isActive()) m_animTimer->start();
    }
}

void EntryView::leaveEvent(QEvent* event)
{
    m_hoverIndex = -1;
    if (!m_animTimer->isActive()) m_animTimer->start();
    QWidget::leaveEvent(event);
}

void EntryView::mousePressEvent(QMouseEvent* event)
{
    const int idx = indexAt(event->pos());
    if (idx < 0) {
        if (event->button() == Qt::LeftButton && m_selectedEntryId >= 0) {
            m_selectedEntryId = -1;
            emit entryClicked(nullptr);
        }
        return;
    }

    if (event->button() == Qt::LeftButton) {
        // scrollToEntry centers the tile (clamped to scroll bounds), kicks
        // the smooth-scroll timer, and emits entryClicked. When the tile
        // is already centered the diff is ~0 and the timer self-stops.
        scrollToEntry(idx);
        return;
    }

    if (event->button() == Qt::RightButton) {
        core::Entry* e = m_entries[idx];
        QMenu menu(this);

        QAction* composerAct = nullptr;
        if (!e->images.isEmpty()) {
            const bool composerActive = m_activeEntryIds.contains(e->id);
            composerAct =
                menu.addAction(composerActive ? "Remove from Composer" : "Add to Composer");
        }

        QAction* loraAct = nullptr;
        if (e->lora.has_value()) {
            if (!menu.isEmpty()) menu.addSeparator();
            const bool loraActive = m_loraActiveOrder.contains(e->id);
            loraAct = menu.addAction(loraActive ? "Deactivate LoRA" : "Activate LoRA");
        }

        if (menu.isEmpty()) return;
        QAction* chosen = menu.exec(QCursor::pos());
        if (!chosen) return;

        if (chosen == composerAct) {
            emit tagsExported(e->id, 0, m_model->getTags(e->images[0].tagIds));
        }
        else if (chosen == loraAct) {
            if (m_loraActiveOrder.contains(e->id))
                m_loraActiveOrder.removeAll(e->id);
            else
                m_loraActiveOrder.append(e->id);
            update();
            emitLoraStack();
        }
    }
}

void EntryView::rebuildNavPanel()
{
    if (!m_navPanel) return;
    QList<QPair<QString, int>> items;
    for (int i = 0; i < m_entries.size(); ++i) {
        const core::Entry* e = m_entries[i];
        if (m_activeEntryIds.contains(e->id)) {
            const QString title = e->title.isEmpty() ? QStringLiteral("(untitled)") : e->title;
            items.append({title, i});
        }
    }
    static_cast<EntryNavPanel*>(m_navPanel)->updateEntries(items);
}

void EntryView::selectAndScrollToEntry(int32_t entryId)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i]->id == entryId) {
            scrollToEntry(i);
            return;
        }
    }
}

void EntryView::scrollToEntry(int idx)
{
    if (idx < 0 || idx >= m_entries.size()) return;
    const int row = idx / std::max(1, m_cols);
    const qreal tileTop = PadV + row * static_cast<qreal>(TileH + Spacing);
    const qreal centered = tileTop - (height() - TileH) / 2.0;
    const int maxScroll = std::max(0, m_totalH - height());
    m_scrollYTarget = std::clamp(centered, 0.0, static_cast<qreal>(maxScroll));
    if (!m_animTimer->isActive()) m_animTimer->start();
    m_selectedEntryId = m_entries[idx]->id;
    emit entryClicked(m_entries[idx]);
}

void EntryView::repositionNav()
{
    if (!m_navPanel) return;
    constexpr int margin = 8;
    m_navPanel->move(width() - m_navPanel->width() - margin, margin - 2);
    m_navPanel->raise();
}

void EntryView::setTileGradient(qreal start, int alpha)
{
    m_gradStart = std::clamp(start, 0.0, 1.0);
    m_gradAlpha = std::clamp(alpha, 0, 255);
}

void EntryView::setTileTitleColor(const QColor& color)
{
    if (color.isValid()) m_titleColor = color;
}

void EntryView::clearLoraForEntry(int entryId)
{
    if (m_loraActiveOrder.removeAll(entryId) > 0) {
        update();
        emitLoraStack();
    }
}

void EntryView::emitLoraStack()
{
    QList<core::LoraConfig> stack;
    for (int id : m_loraActiveOrder) {
        core::Entry* e = m_model->entryById(id);
        if (e && e->lora.has_value()) stack << e->lora.value();
    }
    emit loraStackChanged(stack);
}

QList<QString> EntryView::activeLoraUuids() const
{
    QList<QString> result;
    for (int id : m_loraActiveOrder) {
        core::Entry* e = m_model->entryById(id);
        if (e && e->lora.has_value()) result << e->uuid;
    }
    return result;
}

void EntryView::setLoraActiveByUuids(const QList<QString>& uuids)
{
    m_loraActiveOrder.clear();
    for (const QString& uuid : uuids) {
        core::Entry* e = m_model->entryByUuid(uuid);
        if (e && e->lora.has_value()) m_loraActiveOrder.append(e->id);
    }
    update();
    emitLoraStack();
}

// ── paint ─────────────────────────────────────────────────────────────────────

void EntryView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHints(QPainter::SmoothPixmapTransform | QPainter::Antialiasing);

    if (m_entries.isEmpty() || m_cols == 0) return;

    const int strideY = TileH + Spacing;
    const int startRow = std::max(0, (int)((m_scrollYActual - PadV) / strideY));
    const int endRow = (m_scrollYActual + height() - PadV) / strideY + 1;
    const int startIdx = startRow * m_cols;
    const int endIdx = std::min((endRow + 1) * m_cols, (int)m_entries.size());

    for (int i = startIdx; i < endIdx; ++i) {
        const QRect r = tileRect(i);
        if (!r.intersects(rect())) continue;

        QPixmap pix;
        {
            QMutexLocker lk(&m_cacheMutex);
            if (const QPixmap* p = m_pixCache.object(i)) pix = *p;
        }

        if (pix.isNull()) {
            // Draw rounded placeholder; request load
            p.drawPixmap(r.topLeft(), m_placeholder);
            requestLoad(i);
            continue;
        }

        // Retrieve or default-construct anim state.
        // Default: fadeOpacity=1, hoverT=0 - correct for tiles restored from
        // a cache that already existed (e.g. after a re-query with warm cache).
        TileAnim& a = m_anims[i];

        const qreal easedHover = inOutSine(a.hoverT);
        const qreal scale = 1.0 - 0.03 * easedHover;                     // 1.00 → 0.97
        const qreal opacity = a.fadeOpacity * (1.0 - 0.25 * easedHover); // full → 0.75

        p.save();
        p.setOpacity(opacity);
        p.translate(r.center());
        p.scale(scale, scale);
        p.translate(-r.center());
        p.drawPixmap(r.topLeft(), pix);
        if (m_activeEntryIds.contains(m_entries[i]->id)) {
            p.setOpacity(1.0);
            QPen borderPen(QColor(74, 160, 74), 2.5);
            p.setPen(borderPen);
            p.setBrush(Qt::NoBrush);
            QPainterPath borderPath;
            borderPath.addRoundedRect(QRectF(r).adjusted(1.25, 1.25, -1.25, -1.25), Radius, Radius);
            p.drawPath(borderPath);
        }

        {
            const bool hasLora = m_entries[i]->lora.has_value();
            const bool loraActive = m_loraActiveOrder.contains(m_entries[i]->id);
            if (loraActive) {
                p.setOpacity(1.0);
                p.setPen(QPen(QColor(220, 150, 30), 2.5));
                p.setBrush(Qt::NoBrush);
                QPainterPath lp;
                lp.addRoundedRect(QRectF(r).adjusted(4.0, 4.0, -4.0, -4.0), Radius - 2, Radius - 2);
                p.drawPath(lp);
            }
            else if (hasLora) {
                p.setOpacity(0.85);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(220, 150, 30));
                p.drawEllipse(QPointF(r.right() - 9.0, r.top() + 9.0), 5.0, 5.0);
            }
        }

        p.restore();
    }

    // Preload a few rows ahead (outside the visible range)
    const int preloadEnd = std::min(endIdx + m_cols * 3, (int)m_entries.size());
    for (int i = endIdx; i < preloadEnd; ++i) {
        bool needed;
        {
            QMutexLocker lk(&m_cacheMutex);
            needed = !m_pixCache.contains(i) && !m_pending.contains(i);
        }
        if (needed) requestLoad(i);
    }
}

// ── async load ────────────────────────────────────────────────────────────────

void EntryView::requestLoad(int entryIndex)
{
    {
        QMutexLocker lk(&m_cacheMutex);
        if (m_pending.contains(entryIndex)) return;
        m_pending.insert(entryIndex);
    }

    const int generation = m_generation;
    const Entry* e = m_entries[entryIndex];

    if (e->images.isEmpty()) {
        const QImage composed = makeTileImage(m_emptyTileBg, e->title);
        QMutexLocker lk(&m_cacheMutex);
        m_pixCache.insert(entryIndex, new QPixmap(QPixmap::fromImage(composed)));
        m_pending.remove(entryIndex);
        update();
        return;
    }

    const QString path = BASE_PATH + "/data/entry/" + e->uuid + "/" + e->images[0].fileName;
    const QString title = e->title;

    // QtConcurrent::run is [[nodiscard]] in Qt 6 - we don't need the
    // future (the worker hops back via QMetaObject::invokeMethod), so
    // queue directly on the global pool to avoid the warning.
    QThreadPool::globalInstance()->start([this, entryIndex, path, title, generation]() {
        QImage img(path);

        if (img.isNull()) {
            // File missing or unreadable - fall back to a placeholder
            // tile (with title), same as entries with no images at all.
            // Without this, paint requests would refire on every paint
            // and the cache would never fill, looping indefinitely.
            QMetaObject::invokeMethod(
                this,
                [this, entryIndex, title, generation]() {
                    if (m_generation != generation) return;
                    const QImage composed = makeTileImage(m_emptyTileBg, title);
                    QMutexLocker lk(&m_cacheMutex);
                    m_pixCache.insert(entryIndex, new QPixmap(QPixmap::fromImage(composed)));
                    m_pending.remove(entryIndex);
                    update();
                },
                Qt::QueuedConnection);
            return;
        }

        QImage scaled = img.scaled(QSize(TileW, TileH), Qt::KeepAspectRatioByExpanding,
                                   Qt::SmoothTransformation);
        QImage composed = makeTileImage(scaled, title);

        QMetaObject::invokeMethod(
            this,
            [this, entryIndex, generation, composed = std::move(composed)]() {
                if (m_generation != generation) return;
                QPixmap pix = QPixmap::fromImage(composed);
                {
                    QMutexLocker lk(&m_cacheMutex);
                    m_pixCache.insert(entryIndex, new QPixmap(pix));
                    m_pending.remove(entryIndex);
                }

                // Seed the fade-in at 0 - timer will advance it each frame
                m_anims[entryIndex].fadeOpacity = 0.0;

                if (!m_animTimer->isActive()) m_animTimer->start();

                update();
            },
            Qt::QueuedConnection);
    });
}


// ── tile compositor ───────────────────────────────────────────────────────────

QImage EntryView::makeTileImage(const QImage& img, const QString& title)
{
    QImage out(TileW, TileH, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);

    QPainter p(&out);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    // Rounded clip
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, TileW, TileH), Radius, Radius);
    p.setClipPath(clip);
    p.drawImage(QRect(0, 0, TileW, TileH), img);

    // Bottom gradient overlay (start position + bottom-edge alpha come
    // from settings; defaults preserve the original 0.6 / 180 look).
    const qreal startY = TileH * m_gradStart;
    QLinearGradient grad(0, startY, 0, TileH);
    grad.setColorAt(0.0, QColor(0, 0, 0, 0));
    grad.setColorAt(1.0, QColor(0, 0, 0, m_gradAlpha));
    p.fillRect(QRectF(0, startY, TileW, TileH - startY), grad);

    // Title text - set all font properties before setFont/QFontMetrics
    QFont f(QFontDatabase::applicationFontFamilies(0).at(0));
    f.setPointSize(13);
    f.setHintingPreference(QFont::PreferFullHinting);
    f.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(f);
    p.setPen(m_titleColor);

    QFontMetrics fm(f);
    const QString elided = fm.elidedText(title, Qt::ElideRight, TileW - 16);
    p.drawText(QRect(8, TileH - 26, TileW - 16, 22), Qt::AlignLeft | Qt::AlignVCenter, elided);

    return out;
}

} // namespace gui