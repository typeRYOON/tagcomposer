#pragma once
#include <QWidget>
#include <QPixmap>
#include <QElapsedTimer>
#include <QString>
#include <QList>
#include <QSet>

namespace gui {

class DanmakuOverlay : public QWidget {
    Q_OBJECT
public:
    explicit DanmakuOverlay(QWidget* parent = nullptr);
    void setActive(bool active);

protected:
    void paintEvent(QPaintEvent* event) override;
    void timerEvent(QTimerEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Item {
        bool    isImage      = false;
        QString text;
        QPixmap pixmap;
        float   x            = 0;
        float   y            = 0;
        float   speed        = 0;
        float   opacity      = 0;
        int     layer        = 0;
        float   fontSize     = 14;
        // Index into the combined [m_texts] + [m_images] pool. -1 = no
        // content yet (initial state before first spawn). Used to reserve /
        // release an entry in the per-layer in-use set so the same line
        // can't be on-screen twice in the same layer at the same time.
        int     contentIndex = -1;
        // Pixel width of the rendered text or pixmap. Cached at spawn so
        // the per-frame off-screen check doesn't re-measure text every
        // tick — and so long text/wide images don't pop out of view before
        // their right edge actually clears the left side of the widget.
        float   contentWidth = 0.0f;
    };

    void loadContent();
    void populate();
    void spawnItem(Item& item, bool scatter);
    int  chooseIndex(int layer);

    QList<Item>    m_items;
    QList<QString> m_texts;
    QList<QPixmap> m_images;
    // Indices currently on-screen per layer. Spawn picks an index not in
    // the layer's set; the previous index is released first so respawning
    // an item also opens up its old slot.
    QSet<int>      m_layerInUse[3];

    QElapsedTimer m_elapsed;
    int           m_timerId = 0;
    bool          m_active  = false;
};

} // namespace gui
