#include <gui/widgets/statusbar.h>
#include <utils/logger.h>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QTimer>

namespace gui {

StatusBar::StatusBar(QWidget* parent) : QWidget(parent)
{
    setObjectName("StatusBar");
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(22);

    m_label = new QLabel(this);
    m_label->setObjectName("StatusBarLabel");
    m_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    // Status text auto-clears after 10s
    m_clearTimer = new QTimer(this);
    m_clearTimer->setSingleShot(true);
    m_clearTimer->setInterval(10'000);
    connect(m_clearTimer, &QTimer::timeout, m_label, &QLabel::clear);

    m_activeLabel = new QLabel("0 active", this);
    m_activeLabel->setObjectName("StatusBarActiveLabel");
    m_activeLabel->setFixedHeight(14);
    m_activeLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);

    m_progress = new QProgressBar(this);
    m_progress->setObjectName("StatusBarProgress");
    m_progress->setFixedSize(160, 14);
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    m_progress->setFormat("");

    m_activeEffect = new QGraphicsOpacityEffect(m_activeLabel);
    m_activeEffect->setOpacity(0.0);
    m_activeLabel->setGraphicsEffect(m_activeEffect);

    m_activeFade = new QPropertyAnimation(m_activeEffect, "opacity", this);
    m_activeFade->setDuration(350);
    m_activeFade->setEasingCurve(QEasingCurve::InOutQuad);

    m_progressEffect = new QGraphicsOpacityEffect(m_progress);
    m_progressEffect->setOpacity(0.0);
    m_progress->setGraphicsEffect(m_progressEffect);

    m_progressFade = new QPropertyAnimation(m_progressEffect, "opacity", this);
    m_progressFade->setDuration(350);
    m_progressFade->setEasingCurve(QEasingCurve::InOutQuad);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 2, 10, 0);
    layout->setSpacing(8);
    layout->addWidget(m_label, 1);
    layout->addWidget(m_progress);
    layout->addWidget(m_activeLabel);

    connect(&utils::Logger::instance(), &utils::Logger::messageLogged, this,
            &StatusBar::showMessage);
}

void StatusBar::showMessage(const QString& message)
{
    QFontMetrics fm(m_label->font());
    QString elided = fm.elidedText(message, Qt::ElideRight, m_label->width());
    m_label->setText(elided);
    if (message.isEmpty())
        m_clearTimer->stop();
    else
        m_clearTimer->start();
}

void StatusBar::setProgress(int step, int total)
{
    if (step <= 0 || total <= 0) return;
    // Cancelling mid-sample leaves preview frames in flight: they arrive
    // after the status message that emptied the queue and would fade the bar
    // back in on its own, with the count label already hidden. Nothing is
    // running at that point, so drop them.
    if (m_activeCount <= 0) return;
    m_progress->setRange(0, total);
    m_progress->setValue(qMin(step, total));
    m_progress->setFormat(QString("%1 / %2").arg(step).arg(total));
    fadeProgressTo(1.0);
}

void StatusBar::clearProgress()
{
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    m_progress->setFormat("");
    fadeProgressTo(0.0);
}

void StatusBar::setActiveCount(int count)
{
    m_activeCount = count;
    m_activeLabel->setText(QString("%1 active").arg(count));
    if (count > 0) {
        fadeActiveTo(1.0);
    }
    else {
        fadeActiveTo(0.0);
        clearProgress();
    }
}

void StatusBar::fadeProgressTo(qreal opacity)
{
    if (qFuzzyCompare(m_progressEffect->opacity(), opacity)) return;
    m_progressFade->stop();
    m_progressFade->setStartValue(m_progressEffect->opacity());
    m_progressFade->setEndValue(opacity);
    m_progressFade->start();
}

void StatusBar::fadeActiveTo(qreal opacity)
{
    if (qFuzzyCompare(m_activeEffect->opacity(), opacity)) return;
    m_activeFade->stop();
    m_activeFade->setStartValue(m_activeEffect->opacity());
    m_activeFade->setEndValue(opacity);
    m_activeFade->start();
}

} // namespace gui
