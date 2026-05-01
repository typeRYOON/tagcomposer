#include <gui/widgets/statusbar.h>
#include <utils/logger.h>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QProgressBar>
#include <QPropertyAnimation>

namespace gui {

StatusBar::StatusBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("StatusBar");
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(22);

    m_label = new QLabel(this);
    m_label->setObjectName("StatusBarLabel");
    m_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    m_progress = new QProgressBar(this);
    m_progress->setObjectName("StatusBarProgress");
    m_progress->setFixedSize(160, 14);
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    m_progress->setFormat("");

    // Hidden at startup — fades in on first non-idle setProgress, fades back
    // out when progress goes idle. Layout still reserves the 160px slot, so
    // the message label's width stays stable.
    m_progressEffect = new QGraphicsOpacityEffect(m_progress);
    m_progressEffect->setOpacity(0.0);
    m_progress->setGraphicsEffect(m_progressEffect);

    m_progressFade = new QPropertyAnimation(m_progressEffect, "opacity", this);
    m_progressFade->setDuration(350);
    m_progressFade->setEasingCurve(QEasingCurve::InOutQuad);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 0, 10, 0);
    layout->setSpacing(8);
    layout->addWidget(m_label, 1);
    layout->addWidget(m_progress);

    connect(&utils::Logger::instance(), &utils::Logger::messageLogged,
            this, &StatusBar::showMessage);
}

void StatusBar::showMessage(const QString& message)
{
    QFontMetrics fm(m_label->font());
    QString elided = fm.elidedText(message, Qt::ElideRight, m_label->width());
    m_label->setText(elided);
}

void StatusBar::setProgress(int step, int total)
{
    if (step <= 0 || total <= 0) return;  // ignore between-prompt idle reports
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

void StatusBar::fadeProgressTo(qreal opacity)
{
    if (qFuzzyCompare(m_progressEffect->opacity(), opacity)) return;
    m_progressFade->stop();
    m_progressFade->setStartValue(m_progressEffect->opacity());
    m_progressFade->setEndValue(opacity);
    m_progressFade->start();
}

} // namespace gui
