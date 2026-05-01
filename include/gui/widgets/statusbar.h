#pragma once
#include <QWidget>
#include <QLabel>

class QProgressBar;
class QGraphicsOpacityEffect;
class QPropertyAnimation;

namespace gui {

class StatusBar : public QWidget {
    Q_OBJECT
public:
    explicit StatusBar(QWidget* parent = nullptr);

public slots:
    void showMessage(const QString& message);
    // step >= 1 && total >= 1 → updates value + fades the bar in.
    // Anything else is a no-op so transient (0,0) reports between batched
    // prompts don't visually reset the bar; call clearProgress() to dismiss.
    void setProgress(int step, int total);
    // Empties the bar + fades out. Call when the active queue is drained.
    void clearProgress();

private:
    void fadeProgressTo(qreal opacity);

    QLabel*                 m_label          = nullptr;
    QProgressBar*           m_progress       = nullptr;
    QGraphicsOpacityEffect* m_progressEffect = nullptr;
    QPropertyAnimation*     m_progressFade   = nullptr;
};

} // namespace gui
