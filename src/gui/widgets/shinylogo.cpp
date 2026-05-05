#include <gui/widgets/shinylogo.h>
#include <QPainter>
#include <QPaintEvent>
#include <QLinearGradient>
#include <QPropertyAnimation>
#include <QPauseAnimation>
#include <QSequentialAnimationGroup>
#include <QtMath>

namespace gui {

ShinyLogo::ShinyLogo(QWidget* parent) : QWidget(parent)
{
    // Compositing happens in an offscreen QPixmap, so the widget itself
    // just needs to stay transparent.
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAutoFillBackground(false);
}

// ── Configuration ────────────────────────────────────────────────────────────

void ShinyLogo::setLogo(const QPixmap& logo)
{
    m_logo = logo;
    update();
}

void ShinyLogo::setShineColor(const QColor& c)
{
    m_color = c;
    update();
}
void ShinyLogo::setShineWidthFraction(qreal frac)
{
    m_widthFrac = frac;
    update();
}
void ShinyLogo::setShineAngleDegrees(qreal deg)
{
    m_angleDeg = deg;
    update();
}
void ShinyLogo::setShineIntensity(int peak)
{
    m_alpha = peak;
    update();
}

void ShinyLogo::setShineCycle(int sweepMs, int pauseMs)
{
    m_sweepMs = sweepMs;
    m_pauseMs = pauseMs;
    if (m_sweep) m_sweep->setDuration(sweepMs);
    if (m_pause) m_pause->setDuration(pauseMs);
}

void ShinyLogo::setShineProgress(qreal p)
{
    if (qFuzzyCompare(p, m_progress)) return;
    m_progress = p;
    update();
}

// ── Animation lifecycle ──────────────────────────────────────────────────────

void ShinyLogo::rebuildAnimation()
{
    if (m_group) return;

    // Sequential group + Pause stays in lockstep with Qt's animation
    // framework (auto-pause on app blur, etc.) where a QTimer wouldn't.
    m_group = new QSequentialAnimationGroup(this);

    m_sweep = new QPropertyAnimation(this, "shineProgress", this);
    m_sweep->setStartValue(-0.4);
    m_sweep->setEndValue(1.4);
    m_sweep->setEasingCurve(QEasingCurve::InOutSine);
    m_sweep->setDuration(m_sweepMs);

    m_pause = new QPauseAnimation(m_pauseMs, this);

    m_group->addAnimation(m_sweep);
    m_group->addAnimation(m_pause);
    m_group->setLoopCount(-1);
}

void ShinyLogo::startShine()
{
    m_autoStart = true;
    rebuildAnimation();
    // showEvent will start it for us if hidden right now.
    if (isVisible() && m_group->state() != QAbstractAnimation::Running) m_group->start();
}

void ShinyLogo::stopShine()
{
    m_autoStart = false;
    if (m_group && m_group->state() == QAbstractAnimation::Running) m_group->stop();
}

void ShinyLogo::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // Skip if stopShine() was called explicitly; only auto-resume when the
    // caller's intent says we should be running.
    if (m_autoStart && m_group && m_group->state() != QAbstractAnimation::Running) {
        m_group->start();
    }
}

void ShinyLogo::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    // Pause repaints; m_autoStart stays set so showEvent resumes without
    // the caller having to re-issue startShine().
    if (m_group && m_group->state() == QAbstractAnimation::Running) m_group->stop();
}

// ── Sizing ───────────────────────────────────────────────────────────────────

QSize ShinyLogo::sizeHint() const
{
    return m_logo.isNull() ? QSize(200, 200) : m_logo.size();
}

// ── Paint ────────────────────────────────────────────────────────────────────

void ShinyLogo::paintEvent(QPaintEvent*)
{
    if (m_logo.isNull()) return;

    const QSize fit = m_logo.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect target((width() - fit.width()) / 2, (height() - fit.height()) / 2, fit.width(),
                       fit.height());

    // Drawing onto a transparent QPixmap gives SourceAtop the logo's alpha
    // to mask against, independent of the widget's background.
    QPixmap canvas(size() * devicePixelRatioF());
    canvas.setDevicePixelRatio(devicePixelRatioF());
    canvas.fill(Qt::transparent);

    {
        QPainter cp(&canvas);
        cp.setRenderHint(QPainter::Antialiasing);
        cp.setRenderHint(QPainter::SmoothPixmapTransform);

        cp.drawPixmap(target, m_logo, m_logo.rect());

        // Mask the shine to the logo's silhouette only.
        cp.setCompositionMode(QPainter::CompositionMode_SourceAtop);

        // Shine band as a tilted linear gradient: transparent -> peak -> transparent.
        // Tilting the bottom endpoint rightward gives a leaning lighting sweep.
        const qreal w = target.width();
        const qreal h = target.height();
        const qreal halfBand = 0.5 * m_widthFrac * w;
        // m_progress sweeps -0.4 to 1.4 (see m_sweep), so centerX enters
        // and exits off-screen within a single sweep.
        const qreal centerX = m_progress * w;
        const qreal tilt = std::tan(qDegreesToRadians(m_angleDeg)) * h;

        QLinearGradient grad(target.left() + centerX - halfBand, target.top(),
                             target.left() + centerX + halfBand + tilt, target.bottom());
        QColor c = m_color;
        c.setAlpha(0);
        grad.setColorAt(0.0, c);
        c.setAlpha(m_alpha);
        grad.setColorAt(0.5, c);
        c.setAlpha(0);
        grad.setColorAt(1.0, c);

        cp.fillRect(target, grad);
    }

    QPainter p(this);
    p.drawPixmap(0, 0, canvas);
}

} // namespace gui
