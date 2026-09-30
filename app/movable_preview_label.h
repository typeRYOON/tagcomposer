#pragma once
#include <QLabel>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QSize>

namespace tc {

// The preview tile in the popout window: shows an image, and can be moved,
// resized and dragged out of.
//
// Left click, on the image or on the corner grip without dragging, opens the
// newest image in the output folder, falling back to the shown file. Left
// press and drag starts a copy-style URL drag of the shown file, so it can be
// dropped onto an image slot. Right drag moves the label inside its bounds,
// and a left drag from the corner grip resizes it.
class MovablePreviewLabel : public QLabel {
    Q_OBJECT

public:
    explicit MovablePreviewLabel(QWidget* parent = nullptr);

    void setFilePath(const QString& path);
    void setSourcePixmap(const QPixmap& pixmap);

    // Where finished images land. A click opens the newest image under it,
    // recursively, rather than the frame currently on screen.
    void setOutputFolder(const QString& path);

    // Where the shown preview frames come from. Only the folder button's
    // fallback, for when the output folder is unset or does not exist yet.
    void setTempFolder(const QString& path);

    // The bounds, in parent coordinates, that a move or resize is held
    // inside. Defaults to the parent's rect.
    void setMovableBounds(const QRect& bounds);
    bool isUserPlaced() const;
    void clampToBounds();

    // Places the label at the bottom left of its bounds, sized to the source
    // pixmap's aspect ratio, or square when there is none. Does nothing once
    // the user has moved or resized it by hand.
    void autoFit();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void updateScaled();

    // The newest image in the output folder, else the shown file. An empty
    // target does nothing.
    void openPreferredTarget() const;

    // The output folder, else the temp folder, skipping either if missing.
    void openFolder() const;

    QRect gripRect() const;
    QRect outputButtonRect() const;
    QRect effectiveBounds() const;
    void updateHoverCursor(const QPoint& pos);

    QString m_path;
    QString m_outputFolder;
    QString m_tempFolder;
    QPixmap m_source;
    QPixmap m_outputIcon;

    QPoint m_pressPos;
    bool m_dragInFlight = false;

    // The press landed on the folder button, so the release opens the folder
    // and must not fall through to opening the image.
    bool m_pressOnFolderButton = false;

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

} // namespace tc
