#pragma once
#include <QWidget>
#include <QLabel>

class QProgressBar;
class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QTimer;

namespace gui {

class StatusBar : public QWidget {
    Q_OBJECT
public:
    explicit StatusBar(QWidget* parent = nullptr);

public slots:
    void showMessage(const QString& message);
    // step >= 1 && total >= 1: update value and fade the bar in. Anything
    // else is a no-op, so transient (0,0) reports between batched prompts
    // don't reset the bar - call clearProgress() to dismiss.
    void setProgress(int step, int total);
    void clearProgress();
    // count > 0 fades the label in; count == 0 fades both label and bar out.
    void setActiveCount(int count);

private:
    void fadeProgressTo(qreal opacity);
    void fadeActiveTo(qreal opacity);

    QLabel* m_label = nullptr;
    QTimer* m_clearTimer = nullptr; // auto-clears m_label
    QLabel* m_activeLabel = nullptr;
    QGraphicsOpacityEffect* m_activeEffect = nullptr;
    QPropertyAnimation* m_activeFade = nullptr;
    QProgressBar* m_progress = nullptr;
    QGraphicsOpacityEffect* m_progressEffect = nullptr;
    QPropertyAnimation* m_progressFade = nullptr;
};

} // namespace gui
