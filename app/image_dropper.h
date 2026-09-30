#pragma once
#include <QLabel>
#include <QPixmap>

namespace tc {

// The entry's image slot: shows the current image, accepts a dropped one, and
// opens the file in the system viewer on click. Drawn rather than styled so
// the rounded clip and the empty-state dashes match the tiles.
class ImageDropper : public QLabel {
    Q_OBJECT

public:
    explicit ImageDropper(QWidget* parent = nullptr);

    void setImage(const QString& path);
    void clearImage();

signals:
    void imageDropped(const QString& path);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QPixmap m_pixmap;
    QString m_path;
    bool m_dragOver = false;
};

} // namespace tc
