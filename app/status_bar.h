#pragma once
#include <QWidget>

class QGraphicsOpacityEffect;
class QLabel;
class QProgressBar;
class QPropertyAnimation;
class QTimer;

namespace tc {

// The bottom info bar: a message that clears itself, a queue count, and a
// progress bar. Both the count and the bar fade rather than popping.
//
// It is told things; it does not fetch them. The old one connected itself to
// a global Logger singleton in its constructor, which made the bar impossible
// to use without that singleton existing.
class StatusBar : public QWidget {
    Q_OBJECT

public:
    explicit StatusBar(QWidget* parent = nullptr);

public slots:
    void showMessage(const QString& message);

    // Ignored unless something is queued, so a preview frame arriving after
    // the queue drained cannot fade the bar back in on its own.
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
