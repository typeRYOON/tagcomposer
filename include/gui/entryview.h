#pragma once
#include <core/entry.h>
#include <model/entrymodel.h>
#include <QWidget>
#include <QPixmap>
#include <QHash>
#include <QSet>
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
        explicit EntryView(model::EntryModel* model, QWidget* parent = nullptr);

    public slots:
        void query(const QString& q);

        // signals:
        //     void entryClicked(core::Entry*);

    protected:
        void paintEvent(QPaintEvent* event) override;
        void wheelEvent(QWheelEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void leaveEvent(QEvent* event) override;
        void mousePressEvent(QMouseEvent* event) override;
        void resizeEvent(QResizeEvent* event) override;

    private:
        // Layout helpers
        void  recomputeLayout();
        QRect tileRect(int index) const;
        int   indexAt(QPoint widgetPos) const;

        // Async image loading
        void   requestLoad(int entryIndex);
        QImage makeTileImage(const QImage& src, const QString& title);

        // Model
        model::EntryModel* m_model;
        QList<core::Entry*> m_entries;

        // Tile dimensions (constexpr so they're usable in makeTileImage as static)
        static constexpr int TileW = static_cast<int>(180 * 1.3);
        static constexpr int TileH = static_cast<int>(231 * 1.3);
        static constexpr int Spacing = 12;
        static constexpr int Radius = 12;

        // Layout state
        int m_cols = 1;
        qreal m_scrollYTarget = 0.0;
        qreal m_scrollYActual = 0.0;
        int m_totalH = 0;

        // Hover
        int m_hoverIndex = -1;

        // Pixel cache (key = entry index in m_entries)
        QHash<int, QPixmap> m_pixCache;
        QSet<int>           m_pending;
        QMutex              m_cacheMutex;

        QPixmap m_placeholder;

        // Per-tile animation state
        struct TileAnim {
            qreal fadeOpacity = 1.0;  // 0 → 1 on load
            qreal hoverT = 0.0;  // 0 = normal, 1 = hovered (eased in paintEvent)
        };
        QHash<int, TileAnim> m_anims;
        QTimer* m_animTimer = nullptr;

        int m_generation = 0;
    };

} // namespace gui