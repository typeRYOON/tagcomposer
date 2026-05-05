#pragma once
#include <QWidget>
#include <QPixmap>
#include <QColor>

class QSequentialAnimationGroup;
class QPropertyAnimation;
class QPauseAnimation;

namespace gui {

// Logo widget with a shine band that animates left-to-right, masked to the
// logo's alpha so only the visible silhouette catches the light. Uses
// CompositionMode_SourceAtop on an offscreen pixmap, which keeps the result
// independent of widget background or parent transparency.
class ShinyLogo : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal shineProgress READ shineProgress WRITE setShineProgress)
public:
    explicit ShinyLogo(QWidget* parent = nullptr);

    // Source artwork; widget keeps its own copy and re-fits on resize.
    void setLogo(const QPixmap& logo);

    void setShineColor(const QColor& c);
    void setShineWidthFraction(qreal frac);   // band width / image width
    void setShineAngleDegrees(qreal degrees); // 0 = vertical band
    void setShineIntensity(int peakAlpha);    // 0-255 peak opacity
    void setShineCycle(int sweepMs, int pauseMs);

    // startShine sets a "should run when visible" intent so show/hide auto-
    // pause and resume the loop. Doesn't kick anything off until called -
    // callers can time the start (e.g. after a fade-in).
    void startShine();
    void stopShine();

    qreal shineProgress() const
    {
        return m_progress;
    }
    void setShineProgress(qreal p);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void rebuildAnimation();

    QPixmap m_logo;

    // Sweeps from -0.4 to 1.4 so the band enters and exits off-screen
    // instead of popping in/out at the edges.
    qreal m_progress = -0.4;

    QColor m_color{255, 255, 255};
    int m_alpha{200};
    qreal m_widthFrac{0.20};
    qreal m_angleDeg{20.0};
    int m_sweepMs{1500};
    int m_pauseMs{3500};

    QSequentialAnimationGroup* m_group = nullptr;
    QPropertyAnimation* m_sweep = nullptr;
    QPauseAnimation* m_pause = nullptr;
    // True between startShine/stopShine so show/hideEvent only auto-resume
    // when the caller actually wanted the loop running.
    bool m_autoStart = false;
};

} // namespace gui
