#pragma once
#include <QWidget>

class QGraphicsOpacityEffect;
class QLabel;
class QProgressBar;
class QPropertyAnimation;
class QTimer;

namespace tc {

// Bottom bar: a self-clearing message, the queue count and a progress bar.
class StatusBar : public QWidget {
    Q_OBJECT

public:
    explicit StatusBar(QWidget* parent = nullptr);

public slots:
    void showMessage(const QString& message);

    // Ignored while nothing is queued, so a late preview frame can't show it.
    void setProgress(int step, int total);
    void clearProgress();

    // Above zero fades the count in; zero fades it out and clears progress.
    void setActiveCount(int count);

private:
    void fadeTo(QPropertyAnimation* animation, QGraphicsOpacityEffect* effect, qreal opacity);

    QLabel* m_label = nullptr;
    QTimer* m_clearTimer = nullptr;

    QLabel* m_activeLabel = nullptr;
    QGraphicsOpacityEffect* m_activeEffect = nullptr;
    QPropertyAnimation* m_activeFade = nullptr;
    int m_activeCount = 0;

    QProgressBar* m_progress = nullptr;
    QGraphicsOpacityEffect* m_progressEffect = nullptr;
    QPropertyAnimation* m_progressFade = nullptr;
};

} // namespace tc
