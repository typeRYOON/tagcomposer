#pragma once
#include <QLabel>

namespace gui {

// QLabel that supports hover QSS, click signal, and an in-frame step overlay.
class PreviewClickLabel : public QLabel {
    Q_OBJECT
public:
    explicit PreviewClickLabel(QWidget* parent = nullptr);

    // empty = hide overlay
    void setStepText(const QString& text);

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void paintEvent(QPaintEvent* e) override;

private:
    QString m_stepText;
};

} // namespace gui
