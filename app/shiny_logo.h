#pragma once
#include <QColor>
#include <QPixmap>
#include <QWidget>

class QPauseAnimation;
class QPropertyAnimation;
class QSequentialAnimationGroup;

namespace tc {

// A logo with a shine band sweeping left to right, masked to the artwork's
// alpha so only the silhouette catches the light. Compositing happens on an
// offscreen pixmap, which keeps the result independent of whatever is behind
// the widget.
class ShinyLogo : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal shineProgress READ shineProgress WRITE setShineProgress)

public:
    explicit ShinyLogo(QWidget* parent = nullptr);

    void setLogo(const QPixmap& logo);

    // Running is an intent, not a state: hiding the widget pauses the loop and
    // showing it resumes, without the caller re-issuing startShine().
    void startShine();
    void stopShine();

    qreal shineProgress() const;
    void setShineProgress(qreal progress);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void buildAnimation();

    QPixmap m_logo;

    // Sweeps -0.4 to 1.4 so the band enters and leaves off-screen rather than
    // popping in at the edges.
    qreal m_progress = -0.4;

    QSequentialAnimationGroup* m_group = nullptr;
    bool m_wantRunning = false;
};

} // namespace tc
