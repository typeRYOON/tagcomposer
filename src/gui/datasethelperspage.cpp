#include <gui/datasethelperspage.h>
#include <gui/tagclusterpage.h>
#include <QHBoxLayout>

namespace gui {

DatasetHelpersPage::DatasetHelpersPage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("DatasetHelpersPage");
    setAttribute(Qt::WA_StyledBackground, true);

    auto* tabBar = new QWidget(this);
    tabBar->setObjectName("DatasetTabBar");
    tabBar->setAttribute(Qt::WA_StyledBackground, true);
    tabBar->setFixedWidth(130);

    m_tabLayout = new QVBoxLayout(tabBar);
    m_tabLayout->setContentsMargins(6, 8, 6, 8);
    m_tabLayout->setSpacing(4);
    m_tabLayout->setAlignment(Qt::AlignTop);

    m_tabGroup = new QButtonGroup(this);
    m_tabGroup->setExclusive(true);

    m_stack = new QStackedWidget(this);

    addTab("Tag Cluster", new TagClusterPage(this));

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(tabBar);
    root->addWidget(m_stack, 1);
}

void DatasetHelpersPage::addTab(const QString& label, QWidget* page)
{
    const int idx = m_stack->count();

    auto* btn = new QPushButton(label, this);
    btn->setObjectName("DatasetTabBtn");
    btn->setCheckable(true);
    btn->setFixedHeight(32);
    if (idx == 0) btn->setChecked(true);

    m_tabGroup->addButton(btn);
    m_tabLayout->addWidget(btn);
    m_stack->addWidget(page);

    connect(btn, &QPushButton::clicked, m_stack, [this, idx]() {
        m_stack->setCurrentIndex(idx);
    });
}

} // namespace gui
