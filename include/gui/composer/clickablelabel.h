#pragma once
#include <QLabel>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QSize>

namespace gui {

// QLabel with a source pixmap and file path. Left-click - on the image or on
// the corner grip without dragging - opens the newest image in the output
// folder, falling back to the shown file; left-press + drag (outside the
// corner grip) starts a copy-style URL drag of the shown file so it can be
// dropped onto an ImageDropper. Right-button drag moves the label within its
// movable bounds; left-drag from the corner grip resizes it.
class ClickableLabel : public QLabel {
public:
    explicit ClickableLabel(QWidget* parent = nullptr);

    void setFilePath(const QString& path);
    void setSourcePixmap(const QPixmap& pix);
    // Where finished images land. Clicks on the label open the newest image
    // under it (recursively) instead of the shown file.
    void setOutputFolder(const QString& path);
    // Where the shown preview frames come from. Only used as the folder
    // button's fallback when the output folder is unset or missing.
    void setTempFolder(const QString& path);

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
    // Newest output-folder image, else the shown file. Empty target = no-op.
    void openPreferredTarget() const;
    // Output folder, else temp folder. Skips folders that don't exist.
    void openFolder() const;
    QRect gripRect() const;
    QRect outputBtnRect() const;
    QRect effectiveBounds() const;
    void updateHoverCursor(const QPoint& pos);

    QString m_path;
    QString m_outputFolder;
    QString m_tempFolder;
    QPixmap m_src;
    QPixmap m_outputIcon;
    QPoint m_pressPos;
    bool m_dragInFlight = false;
    // Press landed on the folder button; the release opens the folder and
    // must not fall through to the image-opening path.
    bool m_pressOnFolderBtn = false;

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
