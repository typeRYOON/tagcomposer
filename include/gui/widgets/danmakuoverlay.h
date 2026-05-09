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
        bool isImage = false;
        QString text;
        QPixmap pixmap;
        float x = 0;
        float y = 0;
        float speed = 0;
        float opacity = 0;
        int layer = 0;
        float fontSize = 14;
        // Index into the combined m_texts + m_images pool. -1 means no
        // content yet; tracked in m_layerInUse to keep the same line from
        // running twice in the same layer at once.
        int contentIndex = -1;
        // Cached at spawn so the per-frame off-screen check doesn't re-
        // measure text and wide content doesn't disappear before its right
        // edge actually clears the left side.
        float contentWidth = 0.0f;
    };

    void loadContent();
    void populate();
    void spawnItem(Item& item, bool scatter);
    int chooseIndex(int layer, bool allowImages);

    QList<Item> m_items;
    QList<QString> m_texts;
    QList<QPixmap> m_images;
    // Indices on-screen per layer; spawn picks one not in the layer's set.
    QSet<int> m_layerInUse[3];

    QElapsedTimer m_elapsed;
    int m_timerId = 0;
    bool m_active = false;
};

} // namespace gui
