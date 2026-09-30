#include <app/entry_nav_panel.h>
#include <app/icons.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVariant>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

constexpr int kRowHeight = 24;
constexpr int kMaxHeight = 1000;

} // namespace

EntryNavPanel::EntryNavPanel(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(u"EntryNavPanel"_s);
    setFixedWidth(160);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Painted icon beside the caption; QSS can't recolor it.
    auto* handleRow = new QWidget(this);
    handleRow->setFixedHeight(26);

    auto* handleLayout = new QHBoxLayout(handleRow);
    handleLayout->setContentsMargins(0, 0, 0, 0);
    handleLayout->setSpacing(6);
    handleLayout->addStretch();

    auto* handleIcon = new QLabel(handleRow);
    handleIcon->setPixmap(icons::menuLines(12, QColor(0x55, 0x55, 0x55)).pixmap(12, 12));
    handleLayout->addWidget(handleIcon);

    m_handle = new QLabel(u"Active Entries"_s, handleRow);
    m_handle->setObjectName(u"EntryNavHandle"_s);
    handleLayout->addWidget(m_handle);
    handleLayout->addStretch();

    root->addWidget(handleRow);

    m_listFrame = new QWidget(this);
    m_listFrame->setObjectName(u"EntryNavList"_s);
    m_listLayout = new QVBoxLayout(m_listFrame);
    m_listLayout->setContentsMargins(0, 2, 0, 2);
    m_listLayout->setSpacing(0);

    root->addWidget(m_listFrame);
    m_listFrame->setMaximumHeight(0);

    m_animation = new QPropertyAnimation(m_listFrame, "maximumHeight", this);
    m_animation->setEasingCurve(QEasingCurve::InOutQuad);
    m_animation->setDuration(160);

    // Resize with the list so rows aren't clipped.
    connect(m_animation, &QPropertyAnimation::valueChanged, m_listFrame,
            [this](const QVariant&) { adjustSize(); });
}

void EntryNavPanel::setEntries(const QList<QPair<QString, int>>& items)
{
    while (m_listLayout->count()) {
        QLayoutItem* item = m_listLayout->takeAt(0);
        if (QWidget* widget = item->widget()) widget->deleteLater();
        delete item;
    }

    for (const auto& [title, index] : items) {
        auto* button = new QPushButton(title, m_listFrame);
        button->setObjectName(u"EntryNavBtn"_s);
        button->setFixedHeight(kRowHeight);
        button->setCursor(Qt::PointingHandCursor);
        button->setFlat(true);

        connect(button, &QPushButton::clicked, button, [this, index]() {
            if (onEntryClicked) onEntryClicked(index);
        });
        m_listLayout->addWidget(button);
    }

    // Capped so a long list can't outgrow the window.
    m_fullHeight =
        items.isEmpty() ? 0 : std::min(int(items.size()) * kRowHeight + 4, kMaxHeight);
    if (m_listFrame->maximumHeight() > 0) m_listFrame->setMaximumHeight(m_fullHeight);

    adjustSize();
}

void EntryNavPanel::enterEvent(QEnterEvent*)
{
    if (m_fullHeight == 0) return;
    m_animation->stop();
    m_animation->setStartValue(m_listFrame->maximumHeight());
    m_animation->setEndValue(m_fullHeight);
    m_animation->start();
}

void EntryNavPanel::leaveEvent(QEvent*)
{
    m_animation->stop();
    m_animation->setStartValue(m_listFrame->maximumHeight());
    m_animation->setEndValue(0);
    m_animation->start();
}

} // namespace tc
