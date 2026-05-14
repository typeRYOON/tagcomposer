#pragma once
#include <core/savedstate.h>
#include <QCache>
#include <QColor>
#include <QHash>
#include <QMutex>
#include <QPixmap>
#include <QSet>
#include <QTimer>
#include <QWidget>

class QPaintEvent;
class QResizeEvent;
class QWheelEvent;
class QMouseEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QHideEvent;
class QShowEvent;

namespace gui {

// Tile grid for SavedState entries. Modeled on EntryView's custom paint
// pipeline so it gets the same smooth scroll, hover fade, centered layout,
// and async-loaded tile cache without the QListWidget IconMode quirks.
class StatesGridView : public QWidget {
    Q_OBJECT
public:
    explicit StatesGridView(QWidget* parent = nullptr);

    // Replace the model. Triggers a full re-layout. The view never holds
    // ownership of the states; callers must outlive the view (PromptComposerPage
    // owns the StateManager).
    void setStates(const QList<core::SavedState>* states);
    void setFilter(const QString& filter);

    // Tile composition controls (mirrored from settings; entry-view recipe).
    void setTileGradient(qreal start, int alpha);
    void setTileTitleColor(const QColor& color);

    // Drop tile size. Internal grid math uses these directly.
    void setTileSize(int side);

    // Drop one tile's cached render so the next paint re-bakes it. Used by
    // the host after rename/preview-drop/etc.
    void invalidateTile(const QString& id);
    void clearTileCache();

    // Map a widget-local point back to the model index of the tile under it,
    // or -1 if no tile is under the point.
    int indexAtPoint(QPoint p) const;
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
    // Filtered visible indices, computed from m_states + m_filter.
    void recomputeVisible();
    void recomputeLayout();
    QRect tileRect(int visIdx) const;
    void requestLoad(const QString& id, const QString& name, const QString& previewPath);
    QPixmap renderTilePlaceholder(const QString& name) const;

    static bool isImagePath(const QString& path);

    const QList<core::SavedState>* m_states = nullptr;
    QString m_filter;
    QList<int> m_visible; // indices into *m_states

    // Layout constants - mutable so setTileSize can update.
    int m_tileSide = 352;
    int m_spacing = 20;
    static constexpr int PadV = 16;

    int m_cols = 1;
    int m_offsetX = 0;
    int m_totalH = 0;

    // Pixel-smooth scroll (entry-view approach). Target is what wheel events
    // change; actual eases toward target each animation tick.
    qreal m_scrollYTarget = 0.0;
    qreal m_scrollYActual = 0.0;

    int m_hoverIndex = -1; // visible index (not model index)
    bool m_pressedLeft = false;
    QPoint m_pressPos;
    int m_pressedVis = -1;

    // Tile pixmap cache; m_statesPending guards against duplicate jobs.
    // m_generation invalidates stale async callbacks after a re-layout.
    QCache<QString, QPixmap> m_pixCache;
    QSet<QString> m_pending;
    QMutex m_cacheMutex;
    int m_generation = 0;

    // Fade-in opacity for newly arrived tiles, keyed by state.id.
    QHash<QString, qreal> m_fadeIn;

    QTimer* m_animTimer = nullptr;

    qreal m_gradStart = 0.6;
    int m_gradAlpha = 180;
    QColor m_titleColor = Qt::white;
};

} // namespace gui
