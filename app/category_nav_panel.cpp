#include <app/category_nav_panel.h>
#include <app/icons.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>
#include <QVariant>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

constexpr int kRowHeight = 24;

} // namespace

CategoryNavPanel::CategoryNavPanel(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(u"CategoryNavPanel"_s);
    setFixedWidth(130);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Icon and caption are separate labels: the caption keeps its warn style
    // while the icon is a pixmap, which no colour rule can reach.
    auto* handleRow = new QWidget(this);
    handleRow->setObjectName(u"CategoryNavHandleRow"_s);
    handleRow->setFixedHeight(26);

    auto* handleLayout = new QHBoxLayout(handleRow);
    handleLayout->setContentsMargins(0, 0, 0, 0);
    handleLayout->setSpacing(6);
    handleLayout->addStretch();

    m_handleIcon = new QLabel(handleRow);
    m_handleIcon->setObjectName(u"CategoryNavHandleIcon"_s);
    handleLayout->addWidget(m_handleIcon);

    m_handle = new QLabel(u"Groups"_s, handleRow);
    m_handle->setObjectName(u"CategoryNavHandle"_s);
    handleLayout->addWidget(m_handle);
    handleLayout->addStretch();

    root->addWidget(handleRow);
    setHandleWarn(false);

    m_listFrame = new QWidget(this);
    m_listFrame->setObjectName(u"CategoryNavList"_s);
    m_listLayout = new QVBoxLayout(m_listFrame);
    m_listLayout->setContentsMargins(0, 2, 0, 2);
    m_listLayout->setSpacing(0);
    root->addWidget(m_listFrame);
    m_listFrame->setMaximumHeight(0);

    m_animation = new QPropertyAnimation(m_listFrame, "maximumHeight", this);
    m_animation->setEasingCurve(QEasingCurve::InOutQuad);
    m_animation->setDuration(160);

    // The panel tracks the list as it grows, so it never clips its own rows.
    connect(m_animation, &QPropertyAnimation::valueChanged, m_listFrame,
            [this](const QVariant&) { adjustSize(); });
}

void CategoryNavPanel::setHandleWarn(bool warn)
{
    m_handle->setProperty("warn", warn);
    m_handle->style()->unpolish(m_handle);
    m_handle->style()->polish(m_handle);

    // polish() does not descend into children, so the pixmap is redrawn here.
    m_handleIcon->setPixmap(
        icons::menuLines(12, warn ? QColor(0x66, 0x88, 0xaa) : QColor(0x55, 0x55, 0x55))
            .pixmap(12, 12));
}

void CategoryNavPanel::updateCategories(const QStringList& displayNames,
                                        const QHash<QString, int>& undefinedCounts)
{
    while (m_listLayout->count()) {
        QLayoutItem* item = m_listLayout->takeAt(0);
        if (QWidget* widget = item->widget()) widget->deleteLater();
        delete item;
    }

    bool anyWarn = false;
    for (const QString& name : displayNames) {
        const int undefined = undefinedCounts.value(name, 0);

        auto* button = new QPushButton(
            undefined > 0 ? u"%1  - %2"_s.arg(name).arg(undefined) : name, m_listFrame);
        button->setObjectName(u"CategoryNavBtn"_s);
        button->setFixedHeight(kRowHeight);
        button->setCursor(Qt::PointingHandCursor);
        button->setFlat(true);

        if (undefined > 0) {
            button->setProperty("warn", true);
            anyWarn = true;
        }

        connect(button, &QPushButton::clicked, button, [this, name]() {
            if (onCategoryClicked) onCategoryClicked(name);
        });
        m_listLayout->addWidget(button);
    }

    // Tinting the always-visible handle is how an undefined tag gets noticed
    // without expanding the panel.
    setHandleWarn(anyWarn);

    m_fullHeight = int(displayNames.size()) * kRowHeight + 4;
    if (m_listFrame->maximumHeight() > 0) m_listFrame->setMaximumHeight(m_fullHeight);
    adjustSize();
}

void CategoryNavPanel::enterEvent(QEnterEvent*)
{
    m_animation->stop();
    m_animation->setStartValue(m_listFrame->maximumHeight());
    m_animation->setEndValue(m_fullHeight);
    m_animation->start();
}

void CategoryNavPanel::leaveEvent(QEvent*)
{
    m_animation->stop();
    m_animation->setStartValue(m_listFrame->maximumHeight());
    m_animation->setEndValue(0);
    m_animation->start();
}

} // namespace tc
