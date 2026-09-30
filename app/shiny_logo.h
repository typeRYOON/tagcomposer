#pragma once
#include <QColor>
#include <QPixmap>
#include <QWidget>

class QPauseAnimation;
class QPropertyAnimation;
class QSequentialAnimationGroup;

namespace tc {

// A logo with a shine band sweeping across, masked to the logo's alpha.
class ShinyLogo : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal shineProgress READ shineProgress WRITE setShineProgress)

public:
    explicit ShinyLogo(QWidget* parent = nullptr);

    void setLogo(const QPixmap& logo);

    // Pauses while hidden and resumes on show.
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

    // -0.4..1.4 so the band enters and leaves off the edges.
    qreal m_progress = -0.4;

    QSequentialAnimationGroup* m_group = nullptr;
    bool m_wantRunning = false;
};

} // namespace tc
