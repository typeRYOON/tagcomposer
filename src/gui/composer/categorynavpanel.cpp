#include <gui/composer/categorynavpanel.h>
#include <QLabel>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QVBoxLayout>
#include <QVariant>

namespace gui {

CategoryNavPanel::CategoryNavPanel(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName("CategoryNavPanel");
    setFixedWidth(130);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_handle = new QLabel("☰  Groups", this);
    m_handle->setObjectName("CategoryNavHandle");
    m_handle->setFixedHeight(26);
    m_handle->setAlignment(Qt::AlignCenter);
    root->addWidget(m_handle);

    m_listFrame = new QWidget(this);
    m_listFrame->setObjectName("CategoryNavList");
    m_listLayout = new QVBoxLayout(m_listFrame);
    m_listLayout->setContentsMargins(0, 2, 0, 2);
    m_listLayout->setSpacing(0);
    root->addWidget(m_listFrame);
    m_listFrame->setMaximumHeight(0);

    m_anim = new QPropertyAnimation(m_listFrame, "maximumHeight", this);
    m_anim->setEasingCurve(QEasingCurve::InOutQuad);
    m_anim->setDuration(160);
    // Resize panel as list height changes so the widget tracks content
    connect(m_anim, &QPropertyAnimation::valueChanged, m_listFrame,
            [this](const QVariant&) { adjustSize(); });
}

void CategoryNavPanel::updateCategories(const QStringList& displayNames)
{
    while (m_listLayout->count()) {
        auto* item = m_listLayout->takeAt(0);
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }

    for (const QString& name : displayNames) {
        auto* btn = new QPushButton(name, m_listFrame);
        btn->setObjectName("CategoryNavBtn");
        btn->setFixedHeight(24);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFlat(true);
        connect(btn, &QPushButton::clicked, btn, [this, name]() {
            if (onCategoryClicked) onCategoryClicked(name);
        });
        m_listLayout->addWidget(btn);
    }

    m_fullHeight = displayNames.size() * 24 + 4;
    // If already expanded, update live
    if (m_listFrame->maximumHeight() > 0) m_listFrame->setMaximumHeight(m_fullHeight);
    adjustSize();
}

void CategoryNavPanel::enterEvent(QEnterEvent*)
{
    m_anim->stop();
    m_anim->setStartValue(m_listFrame->maximumHeight());
    m_anim->setEndValue(m_fullHeight);
    m_anim->start();
}

void CategoryNavPanel::leaveEvent(QEvent*)
{
    m_anim->stop();
    m_anim->setStartValue(m_listFrame->maximumHeight());
    m_anim->setEndValue(0);
    m_anim->start();
}

} // namespace gui
