#pragma once
#include <QLabel>
#include <QPixmap>

namespace gui {

class ImageDropper : public QLabel {
    Q_OBJECT
public:
    explicit ImageDropper(QWidget* parent = nullptr);
    void setImage(const QString& path);
    void clearImage();

signals:
    void imageDropped(const QString& path);

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragLeaveEvent(QDragLeaveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;

private:
    QPixmap m_pixmap;
    QString m_path;
    bool    m_dragOver = false;
};

} // namespace gui
