#include <app/dataset_helpers_page.h>
#include <app/auto_tag_page.h>
#include <app/batch_edit_page.h>
#include <app/collector_page.h>
#include <app/tag_cluster_page.h>
#include <app/tag_editor_page.h>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QShowEvent>
#include <QStackedWidget>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

// Matches the Output Viewer and Workflow Editor header, so switching pages
// does not shift the title row.
constexpr int kHeaderHeight = 50;
constexpr int kTabHeight = 32;
constexpr int kIndicatorMs = 220;

} // namespace

DatasetHelpersPage::DatasetHelpersPage(Settings& settings, const TagFacets& facets,
                                       TaggerLibrary& taggers,
                                       const QString& clusterFiltersPath,
                                       const QString& collectionsRoot, QWidget* parent)
    : QWidget(parent)
{
    setObjectName(u"DatasetHelpersPage"_s);
    setAttribute(Qt::WA_StyledBackground, true);

    m_tabBar = new QWidget(this);
    m_tabBar->setObjectName(u"DatasetTabBar"_s);
    m_tabBar->setAttribute(Qt::WA_StyledBackground, true);
    m_tabBar->setFixedHeight(kHeaderHeight);

    m_tabLayout = new QHBoxLayout(m_tabBar);
    m_tabLayout->setContentsMargins(8, 9, 8, 9);
    m_tabLayout->setSpacing(4);
    m_tabLayout->setAlignment(Qt::AlignLeft);

    m_indicator = new QWidget(m_tabBar);
    m_indicator->setObjectName(u"DatasetTabIndicator"_s);
    m_indicator->setAttribute(Qt::WA_StyledBackground, true);
    m_indicator->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_indicator->lower();

    m_tabGroup = new QButtonGroup(this);
    m_tabGroup->setExclusive(true);

    m_stack = new QStackedWidget(this);
    connect(m_stack, &QStackedWidget::currentChanged, this,
            [this](int) { emit tabChanged(currentTabLabel()); });

    // Tab order is the order a dataset is actually built: collect the images,
    // tag them, fix the tags, then apply something across the folder. The
    // cluster tool leads because it is what tells you which tags to want.
    m_clusterPage = new TagClusterPage(facets, clusterFiltersPath, this);
    addTab(u"Tag Cluster"_s, m_clusterPage);

    m_collectorPage = new CollectorPage(settings, collectionsRoot, this);
    addTab(u"Auto-collect"_s, m_collectorPage);

    m_autoTagPage = new AutoTagPage(taggers, settings, this);
    addTab(u"Auto-tagger"_s, m_autoTagPage);

    m_editorPage = new TagEditorPage(settings, this);
    addTab(u"Tag Editor"_s, m_editorPage);

    m_batchPage = new BatchEditPage(settings, this);
    addTab(u"Batch Edit"_s, m_batchPage);

    // The handoffs. Each one carries the folder the previous step produced,
    // because retyping that path is the step people get wrong.
    connect(m_collectorPage, &CollectorPage::sendToAutoTaggerRequested, this,
            [this](const QString& folder) {
                m_autoTagPage->setInputFolder(folder);
                switchTo(m_autoTagPage);
            });
    connect(m_autoTagPage, &AutoTagPage::editFolderRequested, this,
            [this](const QString& folder) {
                m_editorPage->setInputFolder(folder);
                switchTo(m_editorPage);
            });
    connect(m_autoTagPage, &AutoTagPage::sendToBatchEditRequested, this,
            [this](const QString& folder) {
                m_batchPage->setInputFolder(folder);
                switchTo(m_batchPage);
            });
    connect(m_editorPage, &TagEditorPage::sendToBatchEditRequested, this,
            [this](const QString& folder) {
                m_batchPage->setInputFolder(folder);
                switchTo(m_batchPage);
            });

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(m_tabBar);
    root->addWidget(m_stack, 1);
}

void DatasetHelpersPage::setDanbooruIndex(const DanbooruIndex* index)
{
    m_clusterPage->setDanbooruIndex(index);
    m_editorPage->setDanbooruIndex(index);
}

void DatasetHelpersPage::stopBackgroundWork()
{
    m_collectorPage->stopWatcher();
}

TagClusterPage* DatasetHelpersPage::clusterPage() const
{
    return m_clusterPage;
}

void DatasetHelpersPage::setQuickFacets(const QString& character, const QString& copyright,
                                        const QString& triggerWord, const QString& style)
{
    m_clusterPage->setQuickFacets(character, copyright, triggerWord, style);
}

void DatasetHelpersPage::refreshFacets()
{
    m_clusterPage->refreshFacets();
}

void DatasetHelpersPage::addTab(const QString& label, QWidget* page)
{
    const int index = m_stack->count();

    auto* button = new QPushButton(label, m_tabBar);
    button->setObjectName(u"DatasetTabBtn"_s);
    button->setCheckable(true);
    button->setFixedHeight(kTabHeight);
    button->setCursor(Qt::PointingHandCursor);
    if (index == 0) button->setChecked(true);

    m_tabGroup->addButton(button);
    m_tabLayout->addWidget(button);
    m_stack->addWidget(page);

    connect(button, &QPushButton::clicked, this, [this, button, index]() {
        m_stack->setCurrentIndex(index);
        moveIndicatorTo(button, true);
    });
}

void DatasetHelpersPage::switchTo(QWidget* page)
{
    const int index = m_stack->indexOf(page);
    if (index < 0) return;

    m_stack->setCurrentIndex(index);

    // The button order is the layout's, which is addTab order, which is the
    // stack's. QButtonGroup does not promise an order, so it is not used here.
    QLayoutItem* item = m_tabLayout->itemAt(index);
    if (!item) return;
    if (auto* button = qobject_cast<QPushButton*>(item->widget())) {
        button->setChecked(true);
        moveIndicatorTo(button, true);
    }
}

void DatasetHelpersPage::moveIndicatorTo(QWidget* button, bool animate)
{
    if (!button) return;

    const QRect target = button->geometry();
    if (!animate || !m_indicatorPlaced) {
        m_indicator->setGeometry(target);
        m_indicator->show();
        m_indicatorPlaced = true;
        return;
    }

    if (m_indicatorAnim) m_indicatorAnim->stop();
    m_indicatorAnim = new QPropertyAnimation(m_indicator, "geometry", this);
    m_indicatorAnim->setDuration(kIndicatorMs);
    m_indicatorAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_indicatorAnim->setStartValue(m_indicator->geometry());
    m_indicatorAnim->setEndValue(target);
    m_indicatorAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

QString DatasetHelpersPage::currentTabLabel() const
{
    QLayoutItem* item = m_tabLayout->itemAt(m_stack->currentIndex());
    if (!item) return {};
    if (auto* button = qobject_cast<QPushButton*>(item->widget())) return button->text();
    return {};
}

void DatasetHelpersPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    // First show: the buttons have been laid out, so the pill can finally be
    // put somewhere. Later shows already track the checked button.
    if (m_indicatorPlaced) return;
    if (QAbstractButton* button = m_tabGroup->checkedButton()) moveIndicatorTo(button, false);
}

} // namespace tc
