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

// The tile grid: custom-painted, smooth-scrolled, with tiles baked off the
// UI thread into an LRU cache.
//
// It is given a list of uuids to show and looks each entry up as it draws, so
// a result list cannot go stale. The cache is keyed by uuid too, which means a
// re-query keeps every tile it already had warm and a load that lands after
// the list changed is harmless rather than needing a generation counter.
class EntryView : public QWidget {
    Q_OBJECT

public:
    explicit EntryView(const EntryStore& store, QWidget* parent = nullptr);

    void setEntries(const QStringList& uuids);
    void setSelected(const QString& uuid);

    // The uuids currently pushed to the composer; the nav float lists them.
    void setActiveEntries(const QSet<QString>& uuids);

    // The entries whose LoRA is in the composer's stack; drawn as an inner
    // ring, with an unused one showing only a corner dot.
    void setActiveLoras(const QSet<QString>& uuids);

    // Brings one entry into view by uuid; does nothing when it is not in the
    // current result set.
    void scrollToUuid(const QString& uuid);
    QString selected() const;

    // Applied to tiles as they bake, so changing either clears the cache.
    void setTileGradient(qreal start, int alpha);
    void setTileTitleColor(const QColor& colour);

signals:
    void entryClicked(const QString& uuid);

    // Only for a pointer selection. Arrow navigation deliberately does not
    // emit it, so the grid keeps focus for a chained press.
    void entryClickedByPointer(const QString& uuid);

    // The tile menu's LoRA item; the page owns the stack.
    void loraToggled(const QString& uuid);

    // Enter on the selected tile: the page toggles it in the composer.
    void entryActivated(const QString& uuid);

    // Tab with the grid focused: the page parks focus in its search bar.
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

    // Click versus drag: a press records state, a move past the drag
    // threshold promotes to a drag, and a release without one is a click.
    bool m_pressed = false;
    bool m_dragging = false;
    QPoint m_pressPos;
    qreal m_pressScroll = 0.0;

    // Sampled during a drag; left set on release so the tick decays it.
    qreal m_flingVelocity = 0.0;
    qint64 m_lastMoveTime = 0;
    QPoint m_lastMovePos;

    QCache<QString, QPixmap> m_tiles;
    QSet<QString> m_baking;
    mutable QMutex m_tileMutex;

    // Resolved on the UI thread: bakeTile runs on a worker, and QApplication
    // font lookups are not safe to make from one.
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
