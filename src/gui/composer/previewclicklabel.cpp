#include <gui/composer/previewclicklabel.h>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace gui {

PreviewClickLabel::PreviewClickLabel(QWidget* parent) : QLabel(parent)
{
    setAttribute(Qt::WA_Hover);
    setAttribute(Qt::WA_StyledBackground);
    setCursor(Qt::PointingHandCursor);
}

void PreviewClickLabel::setStepText(const QString& text)
{
    if (m_stepText == text) return;
    m_stepText = text;
    update();
}

void PreviewClickLabel::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) emit clicked();
    QLabel::mousePressEvent(e);
}

void PreviewClickLabel::paintEvent(QPaintEvent* e)
{
    QLabel::paintEvent(e);
    if (m_stepText.isEmpty()) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Clip to the inner edge of the 1px border so the overlay never bleeds
    // outside the rounded frame (border-radius: 4px → inner radius ≈ 3px)
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 3.0, 3.0);
    p.setClipPath(clip);

    constexpr int barH = 20;
    p.fillRect(QRect(0, height() - barH, width(), barH), QColor(5, 5, 5, 200));

    p.setPen(QColor(160, 160, 160));
    QFont f;
    f.setPixelSize(11);
    p.setFont(f);
    p.drawText(QRect(0, height() - barH, width() - 6, barH),
               Qt::AlignRight | Qt::AlignVCenter, m_stepText);
}

} // namespace gui
