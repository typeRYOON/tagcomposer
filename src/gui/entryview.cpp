#include <gui/entryview.h>
#include <utils/appconfig.h>
#include <utils/qutils.h>
#include <QtConcurrent>
#include <QFontDatabase>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QTimer>
#include <cmath>
#include <algorithm>

using namespace core;
using namespace model;
using namespace utils;

namespace gui {

    // ── easing ────────────────────────────────────────────────────────────────────

    static qreal inOutSine(qreal t)
    {
        return -(std::cos(M_PI * t) - 1.0) / 2.0;
    }

    // ── ctor ──────────────────────────────────────────────────────────────────────

    EntryView::EntryView(EntryModel* model, QWidget* parent)
        : QWidget(parent), m_model(model)
    {
        setMouseTracking(true);
        setAttribute(Qt::WA_OpaquePaintEvent);

        // Rounded placeholder
        m_placeholder = QPixmap(TileW, TileH);
        m_placeholder.fill(Qt::transparent);
        {
            QPainter pp(&m_placeholder);
            pp.setRenderHint(QPainter::Antialiasing);
            QPainterPath path;
            path.addRoundedRect(QRectF(0, 0, TileW, TileH), Radius, Radius);
            pp.fillPath(path, QColor(40, 40, 40));
        }

        // Single shared animation timer (~60 fps).
        // Drives both load fade-in and hover scale/opacity transitions.
        m_animTimer = new QTimer(this);
        m_animTimer->setInterval(16);
        connect(m_animTimer, &QTimer::timeout, this, [this]()
            {
                bool anyActive = false;

                const qreal scrollDiff = m_scrollYTarget - m_scrollYActual;
                if (std::abs(scrollDiff) > 0.5) {
                    m_scrollYActual += scrollDiff * 0.12;  // 0.12 = scroll smoothing factor
                    anyActive = true;
                }
                else {
                    m_scrollYActual = m_scrollYTarget;  // snap when close enough
                }

                // 500 ms fade-in, 400 ms hover transition
                const qreal fadeStep = 16.0 / 250.0;
                const qreal hoverStep = 16.0 / 200.0;

                for (auto it = m_anims.begin(); it != m_anims.end(); ++it)
                {
                    TileAnim& a = it.value();

                    // Load fade-in
                    if (a.fadeOpacity < 1.0) {
                        a.fadeOpacity = std::min(1.0, a.fadeOpacity + fadeStep);
                        anyActive = true;
                    }

                    // Hover transition — advance toward target (0 or 1)
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
    }

    // ── public ────────────────────────────────────────────────────────────────────

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
        update();
    }

    // ── layout ────────────────────────────────────────────────────────────────────

    void EntryView::recomputeLayout()
    {
        const int strideX = TileW + Spacing;
        m_cols = std::max(1, (width() + Spacing) / strideX);
        int rows = (m_entries.size() + m_cols - 1) / m_cols;
        m_totalH = rows * TileH + std::max(0, rows - 1) * Spacing;
    }

    QRect EntryView::tileRect(int index) const
    {
        const int strideX = TileW + Spacing;
        const int strideY = TileH + Spacing;
        const int col = index % m_cols;
        const int row = index / m_cols;
        return QRect(col * strideX, row * strideY - m_scrollYActual, TileW, TileH);
    }

    int EntryView::indexAt(QPoint p) const
    {
        const int strideX = TileW + Spacing;
        const int strideY = TileH + Spacing;
        const int col = p.x() / strideX;
        const int row = (p.y() + m_scrollYActual) / strideY;

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
        //m_scrollY = std::clamp(m_scrollY, 0, std::max(0, m_totalH - height()));
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
            if (!m_animTimer->isActive())
                m_animTimer->start();
        }
    }

    void EntryView::leaveEvent(QEvent* event)
    {
        m_hoverIndex = -1;
        if (!m_animTimer->isActive())
            m_animTimer->start();
        QWidget::leaveEvent(event);
    }

    void EntryView::mousePressEvent(QMouseEvent* event)
    {
        const int idx = indexAt(event->pos());
        if (idx < 0) return;

        if (event->button() == Qt::LeftButton)
            qDebug() << m_model->getTags(m_entries[idx]->images[0].tagIds);

        // emit entryClicked(m_entries[idx]);
    }

    // ── paint ─────────────────────────────────────────────────────────────────────

    void EntryView::paintEvent(QPaintEvent*)
    {
        QPainter p(this);
        p.setRenderHints(QPainter::SmoothPixmapTransform | QPainter::Antialiasing);
        p.fillRect(rect(), Qt::black);

        if (m_entries.isEmpty() || m_cols == 0) return;

        const int strideY = TileH + Spacing;
        const int startRow = m_scrollYActual / strideY;
        const int endRow = (m_scrollYActual + height()) / strideY + 1;
        const int startIdx = startRow * m_cols;
        const int endIdx = std::min((endRow + 1) * m_cols, (int)m_entries.size());

        for (int i = startIdx; i < endIdx; ++i)
        {
            const QRect r = tileRect(i);
            if (!r.intersects(rect())) continue;

            QPixmap pix;
            {
                QMutexLocker lk(&m_cacheMutex);
                pix = m_pixCache.value(i);
            }

            if (pix.isNull()) {
                // Draw rounded placeholder; request load
                p.drawPixmap(r.topLeft(), m_placeholder);
                requestLoad(i);
                continue;
            }

            // Retrieve or default-construct anim state.
            // Default: fadeOpacity=1, hoverT=0 — correct for tiles restored from
            // a cache that already existed (e.g. after a re-query with warm cache).
            TileAnim& a = m_anims[i];

            const qreal easedHover = inOutSine(a.hoverT);
            const qreal scale = 1.0 - 0.03 * easedHover;          // 1.00 → 0.97
            const qreal opacity = a.fadeOpacity * (1.0 - 0.25 * easedHover); // full → 0.75

            p.save();
            p.setOpacity(opacity);
            p.translate(r.center());
            p.scale(scale, scale);
            p.translate(-r.center());
            p.drawPixmap(r.topLeft(), pix);
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
        const QString path = BASE_PATH + "/data/user/concept/" + e->uuid
            + "/" + e->images[0].fileName;
        const QString title = e->title;

        QtConcurrent::run([this, entryIndex, path, title, generation]()
            {
                QImage img(path);

                if (img.isNull()) {
                    QMetaObject::invokeMethod(this, [this, entryIndex, generation]() {
                        if (m_generation != generation) return;
                        QMutexLocker lk(&m_cacheMutex);
                        m_pending.remove(entryIndex);
                        }, Qt::QueuedConnection);
                    return;
                }

                QImage scaled = img.scaled(
                    QSize(TileW, TileH),
                    Qt::KeepAspectRatioByExpanding,
                    Qt::SmoothTransformation
                );
                QImage composed = makeTileImage(scaled, title);

                QMetaObject::invokeMethod(this,
                    [this, entryIndex, generation, composed = std::move(composed)]()
                    {
                        if (m_generation != generation) return;
                        QPixmap pix = QPixmap::fromImage(composed);
                        {
                            QMutexLocker lk(&m_cacheMutex);
                            m_pixCache[entryIndex] = pix;
                            m_pending.remove(entryIndex);
                        }

                        // Seed the fade-in at 0 — timer will advance it each frame
                        m_anims[entryIndex].fadeOpacity = 0.0;

                        if (!m_animTimer->isActive())
                            m_animTimer->start();

                        update();
                    },
                    Qt::QueuedConnection
                );
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

        // Bottom gradient overlay
        QLinearGradient grad(0, TileH * 0.6, 0, TileH);
        grad.setColorAt(0.0, QColor(0, 0, 0, 0));
        grad.setColorAt(1.0, QColor(0, 0, 0, 180));
        p.fillRect(QRect(0, TileH * 0.6, TileW, TileH * 0.4), grad);

        // Title text — set all font properties before setFont/QFontMetrics
        QFont f(QFontDatabase::applicationFontFamilies(0).at(0));
        f.setPointSize(13);
        f.setHintingPreference(QFont::PreferFullHinting);
        f.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(f);
        p.setPen(Qt::white);

        QFontMetrics fm(f);
        const QString elided = fm.elidedText(title, Qt::ElideRight, TileW - 16);
        p.drawText(QRect(8, TileH - 26, TileW - 16, 22),
            Qt::AlignLeft | Qt::AlignVCenter, elided);

        return out;
    }

} // namespace gui