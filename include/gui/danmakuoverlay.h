#pragma once
#include <QWidget>
#include <QPixmap>
#include <QElapsedTimer>
#include <QString>
#include <QList>

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
        bool    isImage  = false;
        QString text;
        QPixmap pixmap;
        float   x        = 0;
        float   y        = 0;
        float   speed    = 0;
        float   opacity  = 0;
        int     layer    = 0;
        float   fontSize = 14;
    };

    void loadContent();
    void populate();
    void spawnItem(Item& item, bool scatter);

    QList<Item>    m_items;
    QList<QString> m_texts;
    QList<QPixmap> m_images;

    QElapsedTimer m_elapsed;
    int           m_timerId = 0;
    bool          m_active  = false;
};

} // namespace gui
