#pragma once
#include <core/saved_state.h>
#include <QCache>
#include <QColor>
#include <QHash>
#include <QMutex>
#include <QPixmap>
#include <QSet>
#include <QWidget>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QHideEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;
class QTimer;
class QWheelEvent;

namespace tc {

// The tile grid for saved states. Built on the same custom paint pipeline as
// the entry view, so it gets the same smooth scroll, hover fade, centred
// layout and async tile cache without the icon-mode quirks of a list widget.
//
// It never owns the states: the composer owns the StateManager, and the
// pointer handed in has to outlive the view.
class StatesGridView : public QWidget {
    Q_OBJECT

public:
    explicit StatesGridView(QWidget* parent = nullptr);

    void setStates(const QList<SavedState>* states);
    void setFilter(const QString& filter);

    // Tile composition, mirrored from settings.
    void setTileGradient(qreal start, int alpha);
    void setTileTitleColor(const QColor& color);
    void setTileSize(int side);

    // Drops one tile's render so the next paint re-bakes it. Used after a
    // rename or a preview drop.
    void invalidateTile(const QString& id);
    void clearTileCache();

    // The model index of the tile under a widget-local point, or -1.
    int indexAtPoint(QPoint point) const;
    bool isEmpty() const;

signals:
    void tileClicked(int modelIndex);
    void tileEntered(int modelIndex);
    void tileLeft();
    void tileContextMenuRequested(int modelIndex, QPoint globalPos);
    void tileImageDropped(int modelIndex, const QString& sourcePath);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void recomputeVisible();
    void recomputeLayout();
    QRect tileRect(int visibleIndex) const;
    void requestLoad(const QString& id, const QString& name, const QString& previewPath);
    QPixmap renderTilePlaceholder(const QString& name) const;

    static bool isImagePath(const QString& path);

    const QList<SavedState>* m_states = nullptr;
    QString m_filter;
    QList<int> m_visible; // indices into *m_states

    int m_tileSide = 352;
    int m_spacing = 20;
    static constexpr int kPadV = 16;

    int m_columns = 1;
    int m_offsetX = 0;
    int m_totalHeight = 0;

    // Pixel-smooth scroll: the target is what the wheel moves, and the actual
    // eases toward it on every animation tick.
    qreal m_scrollTarget = 0.0;
    qreal m_scrollActual = 0.0;

    int m_hoverIndex = -1; // a visible index, not a model index
    bool m_pressedLeft = false;
    QPoint m_pressPos;
    int m_pressedVisible = -1;

    // m_pending guards against queueing the same tile twice, and the
    // generation counter drops a stale callback after a re-layout.
    QCache<QString, QPixmap> m_pixmapCache;
    QSet<QString> m_pending;
    mutable QMutex m_cacheMutex;
    int m_generation = 0;

    QHash<QString, qreal> m_fadeIn; // per id, for the arrival fade
    QTimer* m_animTimer = nullptr;

    qreal m_gradientStart = 0.6;
    int m_gradientAlpha = 180;
    QColor m_titleColor = Qt::white;
};

} // namespace tc
