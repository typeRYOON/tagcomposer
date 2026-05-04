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
    // Translucent + no auto-fill so the widget composes naturally onto
    // whatever's behind it. The actual alpha-aware compositing is done in
    // an offscreen QPixmap and blit'd to the widget.
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

    // sweep across, pause, repeat. Sequential group + Pause is cleaner than
    // a QTimer since the two phases stay in lockstep with Qt's animation
    // framework (paused on app blur on some platforms, etc.).
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
    // Only actually kick the animation off if we're on-screen. Hidden
    // widgets (e.g. HomePage swapped out of the QStackedWidget) shouldn't
    // burn CPU on a sweep + paint loop the user can't see - the next
    // showEvent will resume.
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
    // Resume only if startShine() set the intent. A widget shown after an
    // explicit stopShine stays paused until startShine is called again.
    if (m_autoStart && m_group && m_group->state() != QAbstractAnimation::Running) {
        m_group->start();
    }
}

void ShinyLogo::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    // Tab switched away (or window minimised) - stop the timer-driven
    // repaints. m_autoStart is left as-is so the next showEvent resumes
    // automatically without the caller having to re-issue startShine.
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

    // Compute the target rect - fit the logo into the widget keeping its
    // aspect ratio, centered.
    const QSize fit = m_logo.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect target((width() - fit.width()) / 2, (height() - fit.height()) / 2, fit.width(),
                       fit.height());

    // Offscreen canvas with alpha. Drawing the logo + the shine onto a
    // transparent QPixmap means SourceAtop has the logo's alpha to mask
    // against, regardless of what the widget's background is doing.
    QPixmap canvas(size() * devicePixelRatioF());
    canvas.setDevicePixelRatio(devicePixelRatioF());
    canvas.fill(Qt::transparent);

    {
        QPainter cp(&canvas);
        cp.setRenderHint(QPainter::Antialiasing);
        cp.setRenderHint(QPainter::SmoothPixmapTransform);

        cp.drawPixmap(target, m_logo, m_logo.rect());

        // Source pixels are only painted where the destination has alpha >
        // 0 - i.e., on the logo's silhouette. Transparent surroundings stay
        // transparent.
        cp.setCompositionMode(QPainter::CompositionMode_SourceAtop);

        // Shine band as a tilted linear gradient. Three stops:
        //   0.0  fully transparent
        //   0.5  peak alpha
        //   1.0  fully transparent
        // The gradient endpoints define the band's perpendicular axis;
        // tilting the bottom point rightward gives a band that leans
        // (visually) like a real lighting sweep.
        const qreal w = target.width();
        const qreal h = target.height();
        const qreal halfBand = 0.5 * m_widthFrac * w;
        // Center moves from −0.4·W (off-screen left) to 1.4·W (off-screen
        // right) over m_progress 0 → 1. m_progress is animated outside that
        // [0, 1] range - see m_sweep's start/end values - so the band
        // already enters and exits the frame within a single sweep.
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

    // Blit the composited canvas to the widget - straight-up SourceOver so
    // alpha is preserved against whatever's behind the widget.
    QPainter p(this);
    p.drawPixmap(0, 0, canvas);
}

} // namespace gui
