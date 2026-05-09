#pragma once
#include <QLabel>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QSize>

namespace gui {

// QLabel with a source pixmap and file path. Left-click opens the file
// in the OS viewer; left-press + drag (outside the corner grip) starts a
// copy-style URL drag so the file can be dropped onto an ImageDropper.
// Right-button drag moves the label within its movable bounds; left-drag
// from the bottom-right corner grip resizes it.
class ClickableLabel : public QLabel {
public:
    explicit ClickableLabel(QWidget* parent = nullptr);

    void setFilePath(const QString& path);
    void setSourcePixmap(const QPixmap& pix);
    // When non-empty, hovering the label reveals a top-left icon button
    // that opens this folder in the OS file manager.
    void setOutputFolder(const QString& path);

    // Bounds (in parent coords) the label is constrained to when the user
    // moves or resizes it. Defaults to parentWidget()->rect().
    void setMovableBounds(const QRect& r);
    bool isUserPlaced() const { return m_userPlaced; }
    void clampToBounds();
    // Auto-place the label in the bottom-left of the current bounds, sized
    // to match the source pixmap's aspect ratio (square fallback when no
    // pixmap is set). No-op once the user has manually moved or resized it.
    void autoFit();

protected:
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void paintEvent(QPaintEvent* e) override;
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    void updateScaled();
    QRect gripRect() const;
    QRect outputBtnRect() const;
    QRect effectiveBounds() const;
    void updateHoverCursor(const QPoint& pos);

    QString m_path;
    QString m_outputFolder;
    QPixmap m_src;
    QPixmap m_outputIcon;
    QPoint m_pressPos;
    bool m_dragInFlight = false;

    enum class Mode { Idle, Moving, Resizing };
    Mode m_mode = Mode::Idle;
    QPoint m_dragStartGlobal;
    QPoint m_dragStartTopLeft;
    QSize m_dragStartSize;
    QRect m_movableBounds;
    bool m_userPlaced = false;
    bool m_hovered = false;
    int m_minSide = 80;
};

} // namespace gui
