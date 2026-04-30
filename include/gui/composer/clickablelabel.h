#pragma once
#include <QLabel>
#include <QPixmap>

namespace gui {

// QLabel that stores a source pixmap (rescales on resize), a file path, and
// opens the file in the OS viewer on left-click.
class ClickableLabel : public QLabel {
public:
    explicit ClickableLabel(QWidget* parent = nullptr);

    void setFilePath(const QString& path);
    void setSourcePixmap(const QPixmap& pix);

protected:
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;

private:
    void updateScaled();

    QString m_path;
    QPixmap m_src;
};

} // namespace gui
