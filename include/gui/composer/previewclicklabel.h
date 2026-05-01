#pragma once
#include <QLabel>

namespace gui {

// QLabel with hover QSS support and a click signal. Used for the inline
// preview tile in the composer; clicking pops out a larger viewer window.
class PreviewClickLabel : public QLabel {
    Q_OBJECT
public:
    explicit PreviewClickLabel(QWidget* parent = nullptr);

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent* e) override;
};

} // namespace gui
