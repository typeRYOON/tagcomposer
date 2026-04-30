#pragma once
#include <QLabel>
#include <QPixmap>

namespace gui {

// QLabel that keeps a source pixmap and rescales it when resized.
class ScaledImageLabel : public QLabel {
public:
    explicit ScaledImageLabel(QWidget* parent = nullptr);

    void setSourcePixmap(const QPixmap& pix);

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    void updateScaled();

    QPixmap m_src;
};

} // namespace gui
