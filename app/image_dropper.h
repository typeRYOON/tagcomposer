#pragma once
#include <QLabel>
#include <QPixmap>

namespace tc {

// The entry image slot: shows the image, accepts drops, opens it on click.
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
