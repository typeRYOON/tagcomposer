#pragma once
#include <QLabel>
#include <QPixmap>
#include <QPoint>

namespace gui {

// QLabel that stores a source pixmap (rescales on resize) and a file path.
// Left-click (no drag) opens the file in the OS viewer. Left-press + drag
// starts a copy-style file drag (URL mime), so the user can drop the temp/
// image onto an ImageDropper to seed an entry.
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
    QPoint  m_pressPos;
    bool    m_dragInFlight = false;
};

} // namespace gui
