#include <app/shiny_logo.h>
#include <QLinearGradient>
#include <QPainter>
#include <QPaintEvent>
#include <QPauseAnimation>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QtMath>

namespace tc {
namespace {

constexpr QColor kShineColour{255, 255, 255};
constexpr int kPeakAlpha = 200;
constexpr qreal kBandWidthFraction = 0.20;
constexpr qreal kTiltDegrees = 20.0;
constexpr int kSweepMs = 1500;
constexpr int kPauseMs = 3500;

} // namespace

ShinyLogo::ShinyLogo(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAutoFillBackground(false);
}

void ShinyLogo::setLogo(const QPixmap& logo)
{
    m_logo = logo;
    update();
}

qreal ShinyLogo::shineProgress() const
{
    return m_progress;
}

void ShinyLogo::setShineProgress(qreal progress)
{
    if (qFuzzyCompare(progress, m_progress)) return;
    m_progress = progress;
    update();
}

QSize ShinyLogo::sizeHint() const
{
    return m_logo.isNull() ? QSize(200, 200) : m_logo.size();
}

void ShinyLogo::buildAnimation()
{
    if (m_group) return;

    m_group = new QSequentialAnimationGroup(this);

    auto* sweep = new QPropertyAnimation(this, "shineProgress", this);
    sweep->setStartValue(-0.4);
    sweep->setEndValue(1.4);
    sweep->setEasingCurve(QEasingCurve::InOutSine);
    sweep->setDuration(kSweepMs);

    m_group->addAnimation(sweep);
    m_group->addAnimation(new QPauseAnimation(kPauseMs, this));
    m_group->setLoopCount(-1);
}

void ShinyLogo::startShine()
{
    m_wantRunning = true;
    buildAnimation();
    if (isVisible() && m_group->state() != QAbstractAnimation::Running) m_group->start();
}

void ShinyLogo::stopShine()
{
    m_wantRunning = false;
    if (m_group && m_group->state() == QAbstractAnimation::Running) m_group->stop();
}

void ShinyLogo::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_wantRunning && m_group && m_group->state() != QAbstractAnimation::Running)
        m_group->start();
}

void ShinyLogo::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (m_group && m_group->state() == QAbstractAnimation::Running) m_group->stop();
}

void ShinyLogo::paintEvent(QPaintEvent*)
{
    if (m_logo.isNull()) return;

    const QSize fit = m_logo.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect target((width() - fit.width()) / 2, (height() - fit.height()) / 2, fit.width(),
                       fit.height());

    // Offscreen so SourceAtop masks against the logo's alpha.
    QPixmap canvas(size() * devicePixelRatioF());
    canvas.setDevicePixelRatio(devicePixelRatioF());
    canvas.fill(Qt::transparent);

    {
        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawPixmap(target, m_logo, m_logo.rect());
        painter.setCompositionMode(QPainter::CompositionMode_SourceAtop);

        const qreal halfBand = 0.5 * kBandWidthFraction * target.width();
        const qreal centreX = m_progress * target.width();
        const qreal tilt = std::tan(qDegreesToRadians(kTiltDegrees)) * target.height();

        QLinearGradient gradient(target.left() + centreX - halfBand, target.top(),
                                 target.left() + centreX + halfBand + tilt, target.bottom());
        QColor colour = kShineColour;
        colour.setAlpha(0);
        gradient.setColorAt(0.0, colour);
        colour.setAlpha(kPeakAlpha);
        gradient.setColorAt(0.5, colour);
        colour.setAlpha(0);
        gradient.setColorAt(1.0, colour);

        painter.fillRect(target, gradient);
    }

    QPainter painter(this);
    painter.drawPixmap(0, 0, canvas);
}

} // namespace tc
