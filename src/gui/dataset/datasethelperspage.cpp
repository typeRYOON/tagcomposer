#include <gui/dataset/datasethelperspage.h>
#include <gui/dataset/tagclusterpage.h>
#include <gui/dataset/autotagpage.h>
#include <gui/dataset/tageditorpage.h>
#include <gui/dataset/batcheditpage.h>
#include <gui/dataset/collectorpage.h>
#include <QPropertyAnimation>
#include <QShowEvent>
#include <QVBoxLayout>

namespace gui {

// Match the OutputViewerPage / WorkflowEditPage section header so flipping
// between pages doesn't shift the title row vertically.
constexpr int kHeaderHeight = 50;
// Animated pill that highlights the currently active tab.
constexpr int kIndicatorAnimMs = 220;

DatasetHelpersPage::DatasetHelpersPage(core::FacetIndex* facets,
                                       core::AutoTaggerLibrary* taggerLibrary,
                                       utils::AppSettings* settings, QWidget* parent)
    : QWidget(parent), m_facets(facets), m_taggerLibrary(taggerLibrary), m_settings(settings)
{
    setObjectName("DatasetHelpersPage");
    setAttribute(Qt::WA_StyledBackground, true);

    m_tabBar = new QWidget(this);
    m_tabBar->setObjectName("DatasetTabBar");
    m_tabBar->setAttribute(Qt::WA_StyledBackground, true);
    m_tabBar->setFixedHeight(kHeaderHeight);

    m_tabLayout = new QHBoxLayout(m_tabBar);
    m_tabLayout->setContentsMargins(8, 9, 8, 9);
    m_tabLayout->setSpacing(4);
    m_tabLayout->setAlignment(Qt::AlignLeft);

    // Sliding pill indicator; outside the layout so we can animate it
    // independently, lowered so the button text paints on top.
    m_indicator = new QWidget(m_tabBar);
    m_indicator->setObjectName("DatasetTabIndicator");
    m_indicator->setAttribute(Qt::WA_StyledBackground, true);
    m_indicator->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_indicator->lower();

    m_tabGroup = new QButtonGroup(this);
    m_tabGroup->setExclusive(true);

    m_stack = new QStackedWidget(this);

    // Tab order matches the typical workflow: build a collection first,
    // then tag, edit, and batch downstream.
    m_tagClusterPage = new TagClusterPage(m_facets, this);
    addTab("Tag Cluster", m_tagClusterPage);

    m_collectorPage = new CollectorPage(m_settings, this);
    addTab("Auto-collect", m_collectorPage);

    m_autoTagPage = new AutoTagPage(m_taggerLibrary, m_settings, this);
    addTab("Auto-tagger", m_autoTagPage);

    // DanbooruIndex is set later via AppMainWindow once the async load
    // finishes - hand a nullptr in so construction proceeds either way.
    m_tagEditorPage = new TagEditorPage(/*danbooru=*/nullptr, m_settings, this);
    addTab("Tag Editor", m_tagEditorPage);

    m_batchEditPage = new BatchEditPage(m_settings, this);
    addTab("Batch Edit", m_batchEditPage);

    // Inter-tab handoff: switch to target's tab and seed its input. Pulled
    // from the layout (preserves addTab order) since QButtonGroup::buttons
    // doesn't document any.
    auto switchToPage = [this](QWidget* target) {
        const int idx = m_stack->indexOf(target);
        if (idx < 0) return;
        m_stack->setCurrentIndex(idx);
        if (auto* item = m_tabLayout->itemAt(idx)) {
            if (auto* btn = qobject_cast<QPushButton*>(item->widget())) {
                btn->setChecked(true);
                moveIndicatorTo(btn, /*animate=*/true);
            }
        }
    };

    // Auto-tagger -> Tag Editor (input)
    connect(m_autoTagPage, &AutoTagPage::editFolderRequested, this,
            [this, switchToPage](const QString& folder) {
                m_tagEditorPage->setInputFolder(folder);
                switchToPage(m_tagEditorPage);
            });

    // Auto-tagger -> Batch Edit (input)
    connect(m_autoTagPage, &AutoTagPage::sendToBatchEditRequested, this,
            [this, switchToPage](const QString& folder) {
                m_batchEditPage->setInputFolder(folder);
                switchToPage(m_batchEditPage);
            });

    // Tag Editor -> Batch Edit (input)
    connect(m_tagEditorPage, &TagEditorPage::sendToBatchEditRequested, this,
            [this, switchToPage](const QString& folder) {
                m_batchEditPage->setInputFolder(folder);
                switchToPage(m_batchEditPage);
            });

    // Auto-collect -> Auto-tagger (input)
    connect(m_collectorPage, &CollectorPage::sendToAutoTaggerRequested, this,
            [this, switchToPage](const QString& folder) {
                m_autoTagPage->setInputFolder(folder);
                switchToPage(m_autoTagPage);
            });

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(m_tabBar);
    root->addWidget(m_stack, 1);
}

void DatasetHelpersPage::addTab(const QString& label, QWidget* page)
{
    const int idx = m_stack->count();

    auto* btn = new QPushButton(label, m_tabBar);
    btn->setObjectName("DatasetTabBtn");
    btn->setCheckable(true);
    btn->setFixedHeight(32);
    if (idx == 0) btn->setChecked(true);

    m_tabGroup->addButton(btn);
    m_tabLayout->addWidget(btn);
    m_stack->addWidget(page);

    connect(btn, &QPushButton::clicked, this, [this, btn, idx]() {
        m_stack->setCurrentIndex(idx);
        moveIndicatorTo(btn, /*animate=*/true);
    });
}

void DatasetHelpersPage::moveIndicatorTo(QWidget* btn, bool animate)
{
    if (!m_indicator || !btn) return;

    const QRect target = btn->geometry();
    if (!animate || !m_indicatorPlaced) {
        m_indicator->setGeometry(target);
        m_indicator->show();
        m_indicatorPlaced = true;
        return;
    }

    if (m_indicatorAnim) m_indicatorAnim->stop();
    m_indicatorAnim = new QPropertyAnimation(m_indicator, "geometry", this);
    m_indicatorAnim->setDuration(kIndicatorAnimMs);
    m_indicatorAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_indicatorAnim->setStartValue(m_indicator->geometry());
    m_indicatorAnim->setEndValue(target);
    m_indicatorAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

void DatasetHelpersPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // First show: place the indicator now that the buttons have been
    // sized. Later shows already track the active button.
    if (!m_indicatorPlaced) {
        if (auto* btn = m_tabGroup->checkedButton()) moveIndicatorTo(btn, /*animate=*/false);
    }
}

} // namespace gui
