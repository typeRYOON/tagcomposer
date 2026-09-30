#pragma once
#include <QCache>
#include <QFont>
#include <QColor>
#include <QHash>
#include <QMutex>
#include <QPixmap>
#include <QSet>
#include <QStringList>
#include <QWidget>

class QTimer;

namespace tc {

class EntryNavPanel;

class EntryStore;

// Custom-painted, smooth-scrolling tile grid. Tiles bake on worker threads into
// an LRU cache keyed by uuid, so a re-query keeps warm tiles.
class EntryView : public QWidget {
    Q_OBJECT

public:
    explicit EntryView(const EntryStore& store, QWidget* parent = nullptr);

    void setEntries(const QStringList& uuids);
    void setSelected(const QString& uuid);

    // Pushed to the composer; listed in the nav float.
    void setActiveEntries(const QSet<QString>& uuids);

    // Entries whose LoRA is in the composer's stack.
    void setActiveLoras(const QSet<QString>& uuids);

    void scrollToUuid(const QString& uuid);
    QString selected() const;

    // Changing either clears the tile cache.
    void setTileGradient(qreal start, int alpha);
    void setTileTitleColor(const QColor& colour);

signals:
    void entryClicked(const QString& uuid);

    // Mouse selection only; arrow keys don't emit it.
    void entryClickedByPointer(const QString& uuid);

    void loraToggled(const QString& uuid);

    // Enter on the selected tile.
    void entryActivated(const QString& uuid);

    // Tab from the grid.
    void focusFilterRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool focusNextPrevChild(bool next) override;

private:
    void rebuildNavPanel();
    void repositionNav();
    void showTileMenu(int index, const QPoint& globalPos);

    void recomputeLayout();
    QRect tileRect(int index) const;
    int indexAt(QPoint pos) const;
    int indexOf(const QString& uuid) const;
    void scrollToIndex(int index);
    void requestBake(int index);
    QImage bakeTile(const QImage& source, const QString& title) const;
    void onAnimationTick();

    const EntryStore* m_store = nullptr;
    QStringList m_uuids;
    QSet<QString> m_activeUuids;
    QSet<QString> m_activeLoraUuids;
    EntryNavPanel* m_navPanel = nullptr;

    static constexpr int kTileW = int(180 * 1.3);
    static constexpr int kTileH = int(231 * 1.3);
    static constexpr int kSpacing = 12;
    static constexpr int kRadius = 12;
    static constexpr int kPadV = 16;

    int m_cols = 1;
    int m_offsetX = 0;
    int m_totalH = 0;
    qreal m_scrollTarget = 0.0;
    qreal m_scrollActual = 0.0;

    int m_hoverIndex = -1;
    QString m_selected;

    // A press becomes a drag past the drag threshold; otherwise it's a click.
    bool m_pressed = false;
    bool m_dragging = false;
    QPoint m_pressPos;
    qreal m_pressScroll = 0.0;

    // Kept after release; the tick decays it.
    qreal m_flingVelocity = 0.0;
    qint64 m_lastMoveTime = 0;
    QPoint m_lastMovePos;

    QCache<QString, QPixmap> m_tiles;
    QSet<QString> m_baking;
    mutable QMutex m_tileMutex;

    // Resolved on the UI thread; bakeTile runs on workers.
    QFont m_tileFont;

    QPixmap m_placeholder;
    QImage m_emptyTile;

    qreal m_gradientStart = 0.6;
    int m_gradientAlpha = 180;
    QColor m_titleColour = Qt::white;

    struct TileAnim {
        qreal fade = 1.0;
        qreal hover = 0.0;
    };
    QHash<int, TileAnim> m_anims;
    QTimer* m_animTimer = nullptr;
};

} // namespace tc
