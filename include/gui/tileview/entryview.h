#pragma once
#include <core/entry.h>
#include <core/entrymodel.h>
#include <QWidget>
#include <QPixmap>
#include <QCache>
#include <QHash>
#include <QSet>
#include <QMap>
#include <QMutex>
#include <QTimer>
#include <QLabel>
#include <QDragEnterEvent>
#include <QMimeData>

namespace gui {

class EntryView : public QWidget {
    Q_OBJECT
public:
    explicit EntryView(core::EntryModel* model, QWidget* parent = nullptr);

public slots:
    void query(const QString& q);
    void setActiveGroups(const QMap<int, QList<int>>& groups);
    void clearLoraForEntry(int entryId);
    void setLoraActiveByUuids(const QList<QString>& uuids);
    // Re-publish the current active stack. Call when an entry's LoraConfig
    // (path, strength, etc.) was edited outside this widget so downstream
    // caches don't keep the stale config.
    void refreshLoraStack() { emitLoraStack(); }
    // Animated scroll to the entry and emit entryClicked; no-op if it
    // isn't in the visible/queried list.
    void selectAndScrollToEntry(int32_t entryId);
    // Startup-only: applied before tiles are baked. Mid-session edits in
    // Settings persist but only show after the next launch.
    void setTileGradient(qreal start, int alpha);
    // Same startup-only semantics as setTileGradient.
    void setTileTitleColor(const QColor& color);

public:
    QList<QString> activeLoraUuids() const;

signals:
    void entryClicked(core::Entry*);
    // Fires only for pointer-driven selections (mouse release, nav panel,
    // external selectAndScrollToEntry). Skipped on keyboard arrow nav so
    // focus can stay on the tile view.
    void entryClickedByPointer(core::Entry*);
    void loraStackChanged(QList<core::LoraConfig> stack);
    void tagsExported(int entryId, int imageIdx, QList<QString> tags);
    // Tab pressed while the grid has focus; the host parks focus in the
    // entry-filter search bar above.
    void focusFilterRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool focusNextPrevChild(bool next) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void recomputeLayout();
    QRect tileRect(int index) const;
    int indexAt(QPoint widgetPos) const;
    void scrollToEntry(int idx, bool fromKeyboard = false);
    void repositionNav();
    void rebuildNavPanel();

    void requestLoad(int entryIndex);
    QImage makeTileImage(const QImage& src, const QString& title);

    core::EntryModel* m_model;
    QList<core::Entry*> m_entries;

    // constexpr so makeTileImage can use them as static.
    static constexpr int TileW = static_cast<int>(180 * 1.3);
    static constexpr int TileH = static_cast<int>(231 * 1.3);
    static constexpr int Spacing = 12;
    static constexpr int Radius = 12;
    static constexpr int PadV = 16;

    int m_cols = 1;
    int m_offsetX = 0;
    qreal m_scrollYTarget = 0.0;
    qreal m_scrollYActual = 0.0;
    int m_totalH = 0;

    int m_hoverIndex = -1;

    // Click-vs-drag tracking. Left-press records state; mouseMove promotes
    // to a drag once it crosses startDragDistance, otherwise mouseRelease
    // fires the click (scrollToEntry / deselect).
    bool m_pressedLeft = false;
    bool m_dragging = false;
    QPoint m_pressPos;
    qreal m_pressScrollY = 0.0;

    // Fling state. mouseMove samples velocity (px/sec) during a drag;
    // mouseRelease leaves it set so the anim timer keeps decaying it
    // and applying it to the scroll position until friction stops it.
    qreal m_flingVelocity = 0.0;
    qint64 m_lastMoveTime = 0;
    QPoint m_lastMovePos;

    // Bounded LRU keyed by entry index; cost is the entry count, since
    // each tile is ~274 KB (TileW * TileH * 4 bytes).
    QCache<int, QPixmap> m_pixCache;
    QSet<int> m_pending;
    QMutex m_cacheMutex;

    QPixmap m_placeholder; // transparent placeholder while a tile loads
    QImage m_emptyTileBg;  // resource-backed; used when an entry has no image

    // Defaults match the original hardcoded look; overridden once at
    // startup from settings.
    qreal m_gradStart = 0.6;
    int m_gradAlpha = 180;
    QColor m_titleColor = Qt::white;

    struct TileAnim {
        qreal fadeOpacity = 1.0; // 0 to 1 on load
        qreal hoverT = 0.0;      // 0 = normal, 1 = hovered (eased in paintEvent)
    };
    QHash<int, TileAnim> m_anims;
    QTimer* m_animTimer = nullptr;

    int m_generation = 0;

    QSet<int> m_activeEntryIds;

    QList<int> m_loraActiveOrder; // entry IDs in activation order (up to maxSlots)

    int m_selectedEntryId = -1;
    QWidget* m_navPanel = nullptr;

    void emitLoraStack();
};

} // namespace gui