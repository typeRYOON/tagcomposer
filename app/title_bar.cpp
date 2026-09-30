#include <app/title_bar.h>
#include <QEasingCurve>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QWindow>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

constexpr auto kAppIcon = ":/icons/taskbar.png";
constexpr auto kMinIcon = ":/icons/title_min.png";
constexpr auto kMaxIcon = ":/icons/title_max.png";
constexpr auto kCloseIcon = ":/icons/title_close.png";

QPropertyAnimation* fade(QWidget* target, qreal from, qreal to)
{
    auto* anim = new QPropertyAnimation(target, "windowOpacity");
    anim->setStartValue(from);
    anim->setEndValue(to);
    anim->setDuration(200);
    anim->setEasingCurve(QEasingCurve::InOutSine);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
    return anim;
}

} // namespace

TitleBar::TitleBar(QWidget* parent) : QWidget(parent)
{
    setObjectName(u"TitleBar"_s);
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(30);

    m_appIcon = new QLabel(this);
    m_appIcon->setObjectName(u"TitleBarAppIcon"_s);
    m_appIcon->setFixedSize(20, 20);
    m_appIcon->setPixmap(QIcon(kAppIcon).pixmap(QSize(18, 18)));
    m_appIcon->setAlignment(Qt::AlignCenter);

    m_title = new QLabel(this);
    m_title->setObjectName(u"TitleBarTitle"_s);
    m_title->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);

    const auto makeButton = [this](const char* icon, const QString& name) {
        auto* button = new QPushButton(this);
        button->setObjectName(name);
        button->setFocusPolicy(Qt::NoFocus);
        button->setFixedSize(46, 30);
        button->setCursor(Qt::ArrowCursor);
        button->setIcon(QIcon(icon));
        button->setIconSize(QSize(12, 12));
        return button;
    };

    m_minButton = makeButton(kMinIcon, u"TitleBarMin"_s);
    m_maxButton = makeButton(kMaxIcon, u"TitleBarMax"_s);
    m_closeButton = makeButton(kCloseIcon, u"TitleBarClose"_s);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(21, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_appIcon);
    layout->addSpacing(32);
    layout->addWidget(m_title, 1);
    layout->addWidget(m_minButton);
    layout->addWidget(m_maxButton);
    layout->addWidget(m_closeButton);

    connect(m_minButton, &QPushButton::clicked, this, [this]() {
        QWidget* host = window();
        if (!host || (host->windowState() & Qt::WindowMinimized)) return;
        // The host fades back in on restore.
        connect(fade(host, host->windowOpacity(), 0.0), &QPropertyAnimation::finished, host,
                [host]() { host->showMinimized(); });
    });
    connect(m_maxButton, &QPushButton::clicked, this, &TitleBar::toggleFullScreen);
    connect(m_closeButton, &QPushButton::clicked, this, [this]() {
        if (QWidget* host = window()) host->close();
    });
}

void TitleBar::setButtons(bool showMin, bool showMax, bool showClose)
{
    m_minButton->setVisible(showMin);
    m_maxButton->setVisible(showMax);
    m_closeButton->setVisible(showClose);
}

void TitleBar::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    if (m_watched != window()) {
        if (m_watched) m_watched->removeEventFilter(this);
        m_watched = window();
        if (m_watched) m_watched->installEventFilter(this);
    }
    refreshTitle();
}

bool TitleBar::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_watched && event->type() == QEvent::WindowTitleChange) refreshTitle();
    return QWidget::eventFilter(obj, event);
}

void TitleBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        if (QWidget* host = window()) {
            if (host->isMaximized()) host->showNormal();
            if (QWindow* handle = host->windowHandle()) {
                handle->startSystemMove();
                event->accept();
                return;
            }
        }
    }
    QWidget::mousePressEvent(event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        toggleFullScreen();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void TitleBar::toggleFullScreen()
{
    QWidget* host = window();
    if (!host) return;

    connect(fade(host, host->windowOpacity(), 0.0), &QPropertyAnimation::finished, host, [host]() {
        if (host->isFullScreen())
            host->showNormal();
        else
            host->showFullScreen();
        fade(host, host->windowOpacity(), 1.0);
    });
}

void TitleBar::refreshTitle()
{
    if (QWidget* host = window()) m_title->setText(host->windowTitle());
}

} // namespace tc
