#pragma once
#include <QWidget>
#include <QPixmap>
#include <QColor>

class QSequentialAnimationGroup;
class QPropertyAnimation;
class QPauseAnimation;

namespace gui {

// Logo widget with a shine band that animates left-to-right across the
// image. The shine is masked to the logo's alpha - transparent pixels stay
// transparent, only the visible silhouette catches the light. Implementation
// is QPainter::CompositionMode_SourceAtop on an offscreen pixmap so the
// composition works regardless of widget background or parent transparency.
//
// Defaults are tuned for a VTuber-style portrait: ~20° tilt, white shine,
// 1.5 s sweep with a 3.5 s pause between cycles.
class ShinyLogo : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal shineProgress READ shineProgress WRITE setShineProgress)
public:
    explicit ShinyLogo(QWidget* parent = nullptr);

    // Source artwork. Pass anything with an alpha channel (PNG/WebP). The
    // widget keeps its own copy and re-fits on every resize.
    void setLogo(const QPixmap& logo);

    // Visual knobs - all optional, sensible defaults applied.
    void setShineColor(const QColor& c);
    void setShineWidthFraction(qreal frac);   // band width / image width
    void setShineAngleDegrees(qreal degrees); // tilt - 0 = vertical band
    void setShineIntensity(int peakAlpha);    // 0–255 peak opacity
    void setShineCycle(int sweepMs, int pauseMs);

    // Auto-loop control. The shine doesn't run until startShine() is called
    // - leaves callers free to time it (e.g., kick off after a fade-in).
    // The animation only actually runs while the widget is visible - when
    // the parent (HomePage) is swapped out of the QStackedWidget, the show
    // /hide events pause and resume it automatically. Calling startShine
    // sets the "should run when visible" intent; stopShine clears it.
    void startShine();
    void stopShine();

    // Direct property access - wired into a QPropertyAnimation by the
    // widget itself, but exposed in case callers want to drive it manually.
    qreal shineProgress() const { return m_progress; }
    void  setShineProgress(qreal p);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent*  event) override;
    void hideEvent(QHideEvent*  event) override;

private:
    void rebuildAnimation();

    QPixmap m_logo;

    // -0.4 → 1.4 keeps the band off-screen at the start/end of a sweep so
    // it enters and exits cleanly instead of popping in/out at the edges.
    qreal m_progress = -0.4;

    QColor m_color    { 255, 255, 255 };
    int    m_alpha     { 200 };
    qreal  m_widthFrac { 0.20 };
    qreal  m_angleDeg  { 20.0 };
    int    m_sweepMs   { 1500 };
    int    m_pauseMs   { 3500 };

    QSequentialAnimationGroup* m_group     = nullptr;
    QPropertyAnimation*        m_sweep     = nullptr;
    QPauseAnimation*           m_pause     = nullptr;
    // Tracks the user's intent independently of visibility - set true by
    // startShine, false by stopShine. showEvent / hideEvent only auto-
    // resume when this is true so a manual stopShine() before showing
    // doesn't get overridden by Qt's first showEvent.
    bool                       m_autoStart = false;
};

} // namespace gui
