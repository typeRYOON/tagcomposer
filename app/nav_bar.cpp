#include <app/nav_bar.h>
#include <app/page.h>
#include <QButtonGroup>
#include <QEnterEvent>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

constexpr int kIconSize = 22;

// SourceIn keeps the alpha and replaces the colour.
QPixmap tinted(const QPixmap& source, QColor colour)
{
    QImage image = source.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(image.rect(), colour);
    painter.end();
    return QPixmap::fromImage(image);
}

} // namespace

NavButton::NavButton(const QString& tooltip, QWidget* parent)
    : QPushButton(parent), m_tooltip(tooltip)
{
    setObjectName(u"NavButton"_s);
    setCheckable(true);
    setFixedSize(44, 44);
    setCursor(Qt::PointingHandCursor);
}

void NavButton::setNavIcon(const QPixmap& pixmap)
{
    if (pixmap.isNull()) return;

    m_idle = tinted(pixmap, QColor(0x66, 0x66, 0x66));
    m_hover = tinted(pixmap, QColor(0xcc, 0xcc, 0xcc));
    m_active = tinted(pixmap, QColor(0xff, 0xff, 0xff));
    setText(QString());
    update();
}

void NavButton::enterEvent(QEnterEvent* event)
{
    QPushButton::enterEvent(event);
    emit hovered(m_tooltip, mapToGlobal(QPoint(width(), height() / 2)));
}

void NavButton::leaveEvent(QEvent* event)
{
    QPushButton::leaveEvent(event);
    emit unhovered();
}

void NavButton::paintEvent(QPaintEvent* event)
{
    QPushButton::paintEvent(event);
    if (m_idle.isNull()) return;

    const QPixmap& icon = isChecked() ? m_active : (underMouse() ? m_hover : m_idle);

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawPixmap(QRect((width() - kIconSize) / 2, (height() - kIconSize) / 2, kIconSize,
                             kIconSize),
                       icon);
}

NavBar::NavBar(QWidget* tooltipParent, QWidget* parent) : QWidget(parent)
{
    setObjectName(u"NavBar"_s);
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(60);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    layout->setAlignment(Qt::AlignTop);

    m_tooltip = new QLabel(tooltipParent);
    m_tooltip->setObjectName(u"NavTooltip"_s);
    m_tooltip->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_tooltip->hide();

    auto* group = new QButtonGroup(this);
    group->setExclusive(true);

    const auto place = [&](NavButton* button) {
        layout->addWidget(button, 0, Qt::AlignHCenter);
    };

    for (int i = 0; i < kPageCount; ++i) {
        if (kPages[i].pinBottom) continue;

        NavButton* button = addButton(QString::fromLatin1(kPages[i].title),
                                      QString::fromLatin1(kPages[i].icon));
        place(button);
        group->addButton(button);
        m_byPage.insert(i, button);
        connect(button, &QPushButton::clicked, this, [this, i]() { emit pageRequested(i); });
    }

    layout->addStretch();

    // Not a page: opens a URL, so it must not latch into a checked state.
    NavButton* discord = addButton(u"Discord"_s, u":/icons/nav_discord.png"_s);
    discord->setCheckable(false);
    place(discord);
    connect(discord, &QPushButton::clicked, this, &NavBar::discordRequested);

    for (int i = 0; i < kPageCount; ++i) {
        if (!kPages[i].pinBottom) continue;

        NavButton* button = addButton(QString::fromLatin1(kPages[i].title),
                                      QString::fromLatin1(kPages[i].icon));
        place(button);
        group->addButton(button);
        m_byPage.insert(i, button);
        connect(button, &QPushButton::clicked, this, [this, i]() { emit pageRequested(i); });
    }

    setCurrentPage(0);
}

NavButton* NavBar::addButton(const QString& tooltip, const QString& iconPath)
{
    auto* button = new NavButton(tooltip, this);
    button->setNavIcon(QPixmap(iconPath));
    connect(button, &NavButton::hovered, this, &NavBar::showTooltip);
    connect(button, &NavButton::unhovered, m_tooltip, &QLabel::hide);
    return button;
}

void NavBar::showTooltip(const QString& text, QPoint globalPos)
{
    QWidget* host = m_tooltip->parentWidget();
    if (!host) return;

    m_tooltip->setText(text);
    m_tooltip->adjustSize();

    const QPoint local = host->mapFromGlobal(globalPos);
    m_tooltip->move(local.x() + 12, local.y() - m_tooltip->height() / 2);
    m_tooltip->raise();
    m_tooltip->show();
}

void NavBar::setCurrentPage(int index)
{
    if (NavButton* button = m_byPage.value(index, nullptr)) button->setChecked(true);
}

} // namespace tc
