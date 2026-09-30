#pragma once
#include <QLabel>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QSize>

namespace tc {

// The popout's preview tile. Click opens the newest output image, left drag
// drags the file out, right drag moves it, the corner grip resizes.
class MovablePreviewLabel : public QLabel {
    Q_OBJECT

public:
    explicit MovablePreviewLabel(QWidget* parent = nullptr);

    void setFilePath(const QString& path);
    void setSourcePixmap(const QPixmap& pixmap);

    // A click opens the newest image under this folder (recursive).
    void setOutputFolder(const QString& path);

    // Folder-button fallback when the output folder is missing.
    void setTempFolder(const QString& path);

    // Move/resize limits in parent coordinates; defaults to the parent rect.
    void setMovableBounds(const QRect& bounds);
    bool isUserPlaced() const;
    void clampToBounds();

    // Bottom-left, sized to the pixmap's aspect; no-op once the user placed it.
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

    // Newest output image, else the shown file.
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

    // The press started on the folder button.
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
