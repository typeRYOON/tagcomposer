#include <app/status_bar.h>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

constexpr int kClearAfterMs = 10'000;
constexpr int kFadeMs = 350;

} // namespace

StatusBar::StatusBar(QWidget* parent) : QWidget(parent)
{
    setObjectName(u"StatusBar"_s);
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(22);

    m_label = new QLabel(this);
    m_label->setObjectName(u"StatusBarLabel"_s);
    m_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    m_clearTimer = new QTimer(this);
    m_clearTimer->setSingleShot(true);
    m_clearTimer->setInterval(kClearAfterMs);
    connect(m_clearTimer, &QTimer::timeout, m_label, &QLabel::clear);

    m_activeLabel = new QLabel(u"0 active"_s, this);
    m_activeLabel->setObjectName(u"StatusBarActiveLabel"_s);
    m_activeLabel->setFixedHeight(14);
    m_activeLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);

    m_progress = new QProgressBar(this);
    m_progress->setObjectName(u"StatusBarProgress"_s);
    m_progress->setFixedSize(160, 14);
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    m_progress->setFormat(QString());

    const auto attachFade = [this](QWidget* target, QGraphicsOpacityEffect*& effect,
                                   QPropertyAnimation*& animation) {
        effect = new QGraphicsOpacityEffect(target);
        effect->setOpacity(0.0);
        target->setGraphicsEffect(effect);

        animation = new QPropertyAnimation(effect, "opacity", this);
        animation->setDuration(kFadeMs);
        animation->setEasingCurve(QEasingCurve::InOutQuad);
    };
    attachFade(m_activeLabel, m_activeEffect, m_activeFade);
    attachFade(m_progress, m_progressEffect, m_progressFade);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 2, 10, 0);
    layout->setSpacing(8);
    layout->addWidget(m_label, 1);
    layout->addWidget(m_progress);
    layout->addWidget(m_activeLabel);
}

void StatusBar::showMessage(const QString& message)
{
    const QFontMetrics metrics(m_label->font());
    m_label->setText(metrics.elidedText(message, Qt::ElideRight, m_label->width()));

    if (message.isEmpty())
        m_clearTimer->stop();
    else
        m_clearTimer->start();
}

void StatusBar::setProgress(int step, int total)
{
    if (step <= 0 || total <= 0 || m_activeCount <= 0) return;

    m_progress->setRange(0, total);
    m_progress->setValue(qMin(step, total));
    m_progress->setFormat(u"%1 / %2"_s.arg(step).arg(total));
    fadeTo(m_progressFade, m_progressEffect, 1.0);
}

void StatusBar::clearProgress()
{
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    m_progress->setFormat(QString());
    fadeTo(m_progressFade, m_progressEffect, 0.0);
}

void StatusBar::setActiveCount(int count)
{
    m_activeCount = count;
    m_activeLabel->setText(u"%1 active"_s.arg(count));

    if (count > 0) {
        fadeTo(m_activeFade, m_activeEffect, 1.0);
        return;
    }
    fadeTo(m_activeFade, m_activeEffect, 0.0);
    clearProgress();
}

void StatusBar::fadeTo(QPropertyAnimation* animation, QGraphicsOpacityEffect* effect,
                       qreal opacity)
{
    if (qFuzzyCompare(effect->opacity(), opacity)) return;

    animation->stop();
    animation->setStartValue(effect->opacity());
    animation->setEndValue(opacity);
    animation->start();
}

} // namespace tc
