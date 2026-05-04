#include <gui/widgets/danmakuoverlay.h>
#include <utils/appconfig.h>
#include <QPainter>
#include <QFontMetrics>
#include <QTimerEvent>
#include <QResizeEvent>
#include <QFile>
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>
#include <QDebug>

namespace gui {

struct LayerConfig {
    float speedMin, speedMax;
    float opacityMin, opacityMax;
    float fontMin, fontMax;
    int count;
};

static constexpr LayerConfig LAYERS[3] = {
    {28, 52, 0.030f, 0.060f, 12, 14, 6},
    {75, 108, 0.080f, 0.120f, 17, 21, 6},
    {145, 185, 0.150f, 0.220f, 23, 27, 6},
};

static float randRange(float lo, float hi)
{
    return lo + float(QRandomGenerator::global()->generateDouble()) * (hi - lo);
}

DanmakuOverlay::DanmakuOverlay(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
    loadContent();
}

void DanmakuOverlay::loadContent()
{
    QFile f(utils::BASE_PATH + "/" + utils::DANMAKU_PATH);
    if (!f.open(QIODevice::ReadOnly)) return;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith('@')) {
            QPixmap px(line.mid(1));
            if (!px.isNull())
                m_images << px.scaled(156, 156, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        else {
            m_texts << line;
        }
    }
}

int DanmakuOverlay::chooseIndex(int layer)
{
    const int total = m_texts.size() + m_images.size();
    if (total == 0) return -1;

    // Build the candidate pool from indices not already on-screen in this
    // layer. If every available line is in use (more items than lines), we
    // fall back to a random pick - duplicates show up only when the user
    // genuinely has fewer lines than items per layer.
    QList<int> candidates;
    candidates.reserve(total);
    for (int i = 0; i < total; ++i) {
        if (!m_layerInUse[layer].contains(i)) candidates.append(i);
    }
    if (candidates.isEmpty()) return QRandomGenerator::global()->bounded(total);
    return candidates[QRandomGenerator::global()->bounded(candidates.size())];
}

void DanmakuOverlay::spawnItem(Item& item, bool scatter)
{
    const LayerConfig& cfg = LAYERS[item.layer];
    item.speed = randRange(cfg.speedMin, cfg.speedMax);
    item.opacity = randRange(cfg.opacityMin, cfg.opacityMax);
    item.fontSize = randRange(cfg.fontMin, cfg.fontMax);

    const float h = std::max(80.0f, float(height()));
    item.y = randRange(cfg.fontMax + 4, h - 4);

    if (scatter)
        item.x = randRange(0, float(width()) * 1.8f); // spread across visible + incoming
    else
        item.x = float(width()) + randRange(0, 300);

    // Release whatever line this item was carrying before we pick a new one,
    // so the slot opens up for any other items in the same layer.
    if (item.contentIndex >= 0) m_layerInUse[item.layer].remove(item.contentIndex);

    const int idx = chooseIndex(item.layer);
    if (idx < 0) return;
    m_layerInUse[item.layer].insert(idx);
    item.contentIndex = idx;

    if (idx < m_texts.size()) {
        item.isImage = false;
        item.text = m_texts[idx];
        item.pixmap = QPixmap{};

        // Cache the rendered text width using the same font we paint with -
        // the off-screen check in timerEvent reads this so wide strings
        // don't pop out of view before their tail clears the left edge.
        QFont font("Hiragino Maru Gothic ProN W4");
        font.setPixelSize(int(item.fontSize));
        item.contentWidth = float(QFontMetrics(font).horizontalAdvance(item.text));
    }
    else {
        item.isImage = true;
        item.pixmap = m_images[idx - m_texts.size()];
        item.text = {};
        item.contentWidth = float(item.pixmap.width());
    }
}

void DanmakuOverlay::populate()
{
    m_items.clear();
    if (m_texts.isEmpty() && m_images.isEmpty()) return;
    for (int layer = 0; layer < 3; ++layer) {
        for (int i = 0; i < LAYERS[layer].count; ++i) {
            Item item;
            item.layer = layer;
            spawnItem(item, true);
            m_items << item;
        }
    }
}

void DanmakuOverlay::setActive(bool active)
{
    if (m_active == active) return;
    m_active = active;
    if (active) {
        setVisible(true);
        if (m_items.isEmpty()) populate();
        m_elapsed.start();
        m_timerId = startTimer(16);
    }
    else {
        if (m_timerId) {
            killTimer(m_timerId);
            m_timerId = 0;
        }
        setVisible(false);
    }
    update();
}

void DanmakuOverlay::timerEvent(QTimerEvent* event)
{
    if (event->timerId() != m_timerId) return;

    const float dt = float(m_elapsed.restart()) / 1000.0f;
    const float clampedDt = std::min(dt, 0.05f);

    // Move items left; respawn once the *right edge* of the content has
    // cleared x = 0. Using the cached contentWidth means a 1200-px-wide
    // string respawns at x ≈ -1200 instead of the old -500 cutoff that was
    // amputating long lines mid-scroll. Tiny safety margin so we don't
    // pop on the exact boundary frame.
    for (auto& item : m_items) {
        item.x -= item.speed * clampedDt;
        if (item.x + item.contentWidth < -8.0f) spawnItem(item, false);
    }

    update();
}

void DanmakuOverlay::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_active && !m_items.isEmpty()) {
        const float h = float(height());
        for (auto& item : m_items) {
            if (item.y > h - 4) item.y = randRange(LAYERS[item.layer].fontMax + 4, h - 4);
        }
    }
}

void DanmakuOverlay::paintEvent(QPaintEvent*)
{
    if (!m_active || m_items.isEmpty()) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    for (const auto& item : m_items) {
        const float drawX = item.x;
        const float drawY = item.y;

        p.setOpacity(double(item.opacity));

        if (item.isImage && !item.pixmap.isNull()) {
            p.drawPixmap(QPointF(drawX, drawY - item.pixmap.height()), item.pixmap);
        }
        else if (!item.text.isEmpty()) {
            QFont font("Hiragino Maru Gothic ProN W4");
            font.setPixelSize(int(item.fontSize));
            p.setFont(font);
            p.setPen(Qt::white);
            p.drawText(QPointF(drawX, drawY), item.text);
        }
    }
}

} // namespace gui
