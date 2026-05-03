#include <gui/widgets/titlebar.h>
#include <utils/qutils.h>
#include <QEasingCurve>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSize>
#include <QWindow>

namespace {
// Icon resource paths. App icon is the existing taskbar .png; the three
// chrome glyphs are PNG files registered in resources.qrc. Sized at draw
// time via setIconSize on each button.
constexpr auto kAppIcon        = ":/icons/taskbar.png";
constexpr auto kIconTitleMin   = ":/icons/title_min.png";
constexpr auto kIconTitleMax   = ":/icons/title_max.png";
constexpr auto kIconTitleClose = ":/icons/title_close.png";
}

namespace gui {

TitleBar::TitleBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("TitleBar");
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(30);

    m_appIcon = new QLabel(this);
    m_appIcon->setObjectName("TitleBarAppIcon");
    m_appIcon->setFixedSize(20, 20);
    m_appIcon->setPixmap(QIcon(kAppIcon).pixmap(QSize(18, 18)));
    m_appIcon->setAlignment(Qt::AlignCenter);

    m_title = new QLabel(this);
    m_title->setObjectName("TitleBarTitle");
    m_title->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);

    auto makeBtn = [this](const QString& iconPath, const QString& objName) {
        auto* b = new QPushButton(this);
        b->setObjectName(objName);
        b->setFocusPolicy(Qt::NoFocus);
        b->setFixedSize(46, 30);
        b->setCursor(Qt::ArrowCursor);
        b->setIcon(QIcon(iconPath));
        b->setIconSize(QSize(12, 12));
        return b;
    };

    m_minBtn   = makeBtn(kIconTitleMin,   "TitleBarMin");
    m_maxBtn   = makeBtn(kIconTitleMax,   "TitleBarMax");
    m_closeBtn = makeBtn(kIconTitleClose, "TitleBarClose");

    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(21, 0, 0, 0);
    h->setSpacing(0);
    h->addWidget(m_appIcon);
    h->addSpacing(32);
    h->addWidget(m_title, 1);
    h->addWidget(m_minBtn);
    h->addWidget(m_maxBtn);
    h->addWidget(m_closeBtn);

    connect(m_minBtn, &QPushButton::clicked, this, [this]() {
        // Fade out, then minimize. Fade-in on un-minimize is handled by the
        // host window's changeEvent (animates back to 1.0 when opacity is
        // low and the window is restored from the taskbar).
        auto* w = window();
        if (!w || (w->windowState() & Qt::WindowMinimized)) return;
        auto* anim = utils::propertyAnimate(w, "windowOpacity",
            w->windowOpacity(), 0.0, 200, QEasingCurve::InOutSine);
        connect(anim, &QPropertyAnimation::finished, w, [w]() {
            w->showMinimized();
        });
    });
    connect(m_maxBtn, &QPushButton::clicked, this, [this]() {
        toggleFullScreen();
    });
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        if (auto* w = window()) w->close();
    });
}

void TitleBar::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_watchedWindow != window()) {
        if (m_watchedWindow) m_watchedWindow->removeEventFilter(this);
        m_watchedWindow = window();
        if (m_watchedWindow) m_watchedWindow->installEventFilter(this);
    }
    refreshTitle();
}

bool TitleBar::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_watchedWindow && event->type() == QEvent::WindowTitleChange) {
        refreshTitle();
    }
    return QWidget::eventFilter(obj, event);
}

void TitleBar::mousePressEvent(QMouseEvent* event)
{
    // Only initiate a drag with the left button; right/middle clicks fall
    // through so future context menus can hook in here.
    if (event->button() == Qt::LeftButton) {
        if (auto* w = window()) {
            // Restore-on-drag (Windows-like): if maximized, restore first so
            // the window doesn't snap back to its old geometry while pinned.
            if (w->isMaximized()) w->showNormal();
            if (auto* h = w->windowHandle()) {
                h->startSystemMove();
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
    // Fade through black across the state change so the OS-level fullscreen
    // transition is hidden behind a smooth opacity dip instead of a snap.
    auto* w = window();
    if (!w) return;
    auto* anim = utils::propertyAnimate(w, "windowOpacity",
        w->windowOpacity(), 0.0, 200, QEasingCurve::InOutSine);
    connect(anim, &QPropertyAnimation::finished, w, [w]() {
        if (w->isFullScreen()) w->showNormal();
        else                   w->showFullScreen();
        utils::propertyAnimate(w, "windowOpacity",
            w->windowOpacity(), 1.0, 200, QEasingCurve::InOutSine);
    });
}

void TitleBar::setButtons(bool showMin, bool showMax, bool showClose)
{
    if (m_minBtn)   m_minBtn  ->setVisible(showMin);
    if (m_maxBtn)   m_maxBtn  ->setVisible(showMax);
    if (m_closeBtn) m_closeBtn->setVisible(showClose);
}

void TitleBar::refreshTitle()
{
    auto* w = window();
    if (!w || !m_title) return;
    m_title->setText(w->windowTitle());
}

} // namespace gui
