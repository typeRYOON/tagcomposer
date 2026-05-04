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

// ── DropLabel ─────────────────────────────────────────────────────────────────

/*class DropLabel : public QLabel {
        Q_OBJECT
    public:
        QString m_droppedPath;
        DropLabel();

    protected:
        void dragEnterEvent(QDragEnterEvent* e) override;
        void dropEvent(QDropEvent* e) override;
    };*/

// ── EntryView ─────────────────────────────────────────────────────────────────

class EntryView : public QWidget {
    Q_OBJECT
public:
    explicit EntryView(core::EntryModel* model, QWidget* parent = nullptr);

public slots:
    void query(const QString& q);
    void setActiveGroups(const QMap<int, QList<int>>& groups);
    void clearLoraForEntry(int entryId);
    void setLoraActiveByUuids(const QList<QString>& uuids);
    // Scroll-with-animation to the given entry and emit entryClicked.
    // No-op if the entry isn't currently in the visible/queried list.
    void selectAndScrollToEntry(int32_t entryId);
    // Set the tile-bottom gradient. Called once at startup from
    // AppMainWindow before any tiles are baked, so no cache clear is
    // needed; mid-session edits in Settings persist but don't apply
    // until the next launch.
    void setTileGradient(qreal start, int alpha);
    // Same startup-only semantics as setTileGradient.
    void setTileTitleColor(const QColor& color);

public:
    QList<QString> activeLoraUuids() const;

signals:
    void entryClicked(core::Entry*);
    void loraStackChanged(QList<core::LoraConfig> stack);
    void tagsExported(int entryId, int imageIdx, QList<QString> tags);

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    // Layout helpers
    void recomputeLayout();
    QRect tileRect(int index) const;
    int indexAt(QPoint widgetPos) const;
    void scrollToEntry(int idx);
    void repositionNav();
    void rebuildNavPanel();

    // Async image loading
    void requestLoad(int entryIndex);
    QImage makeTileImage(const QImage& src, const QString& title);

    // Model
    core::EntryModel* m_model;
    QList<core::Entry*> m_entries;

    // Tile dimensions (constexpr so they're usable in makeTileImage as static)
    static constexpr int TileW = static_cast<int>(180 * 1.3);
    static constexpr int TileH = static_cast<int>(231 * 1.3);
    static constexpr int Spacing = 12;
    static constexpr int Radius = 12;
    static constexpr int PadV = 16;

    // Layout state
    int m_cols = 1;
    int m_offsetX = 0;
    qreal m_scrollYTarget = 0.0;
    qreal m_scrollYActual = 0.0;
    int m_totalH = 0;

    // Hover
    int m_hoverIndex = -1;

    // Pixel cache (key = entry index in m_entries).
    // Bounded LRU - see ctor for max cost. Each tile is ~274 KB
    // (TileW * TileH * 4 bytes), so cost is just the entry count.
    QCache<int, QPixmap> m_pixCache;
    QSet<int> m_pending;
    QMutex m_cacheMutex;

    QPixmap m_placeholder; // transparent - drawn while a tile loads
    QImage m_emptyTileBg;  // resource-backed; fed into makeTileImage
                           // when an entry has no (or a missing) image

    // Bottom-fade gradient + title text colour - defaults match the
    // original hardcoded look, overridden once at startup from settings.
    qreal m_gradStart = 0.6;
    int m_gradAlpha = 180;
    QColor m_titleColor = Qt::white;

    // Per-tile animation state
    struct TileAnim {
        qreal fadeOpacity = 1.0; // 0 → 1 on load
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