#pragma once
#include <QLabel>
#include <QPixmap>
#include <QPoint>

namespace gui {

// QLabel with a source pixmap and file path. Left-click opens the file
// in the OS viewer; left-press + drag starts a copy-style URL drag so
// the file can be dropped onto an ImageDropper.
class ClickableLabel : public QLabel {
public:
    explicit ClickableLabel(QWidget* parent = nullptr);

    void setFilePath(const QString& path);
    void setSourcePixmap(const QPixmap& pix);

protected:
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    void updateScaled();

    QString m_path;
    QPixmap m_src;
    QPoint m_pressPos;
    bool m_dragInFlight = false;
};

} // namespace gui
