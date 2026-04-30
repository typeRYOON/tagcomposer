#include <gui/faceteditorpage.h>
#include <gui/widgets/appscrollbar.h>
#include <gui/widgets/flowlayout.h>
#include <utils/appconfig.h>
#include <QCursor>
#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>
#include <QScrollArea>
#include <QStackedWidget>
#include <QUrl>
#include <algorithm>

namespace gui {

FacetEditorPage::FacetEditorPage(
    core::FacetIndex*  facets,
    core::EntryModel*  model,
    QWidget*           parent)
    : QWidget(parent)
    , m_facets(facets)
    , m_model(model)
{
    setObjectName("FacetEditorPage");
    setAttribute(Qt::WA_StyledBackground, true);

    // ── Left panel ────────────────────────────────────────────────────────────
    auto* leftHeader = new QLabel("TAGS");
    leftHeader->setObjectName("FacetPanelHeader");

    m_searchEdit = new QLineEdit;
    m_searchEdit->setObjectName("FacetSearchBar");
    m_searchEdit->setPlaceholderText("filter...");
    m_searchEdit->setClearButtonEnabled(true);

    m_undefinedHeader = new QLabel;
    m_undefinedHeader->setObjectName("FacetPanelHeader");
    m_undefinedHeader->hide();  // shown by refreshUndefinedList when non-empty

    m_undefinedList = new QListWidget;
    m_undefinedList->setObjectName("FacetTagList");
    m_undefinedList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_undefinedList->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));
    m_undefinedList->setMaximumHeight(160);  // ~6 rows; longer lists scroll
    m_undefinedList->hide();

    m_countLabel = new QLabel;
    m_countLabel->setObjectName("FacetCountLabel");

    m_tagList = new QListWidget;
    m_tagList->setObjectName("FacetTagList");
    m_tagList->setSortingEnabled(true);
    m_tagList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tagList->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));

    // Both lists feed selectTag; clicking one clears the other's highlight
    // so the selection is always visually unambiguous.
    connect(m_tagList, &QListWidget::currentTextChanged,
            this, [this](const QString& text) {
                if (!text.isEmpty()) m_undefinedList->setCurrentItem(nullptr);
                selectTag(text);
            });
    connect(m_undefinedList, &QListWidget::currentTextChanged,
            this, [this](const QString& text) {
                if (!text.isEmpty()) m_tagList->setCurrentItem(nullptr);
                selectTag(text);
            });

    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &FacetEditorPage::applyListFilter);

    // Right-click → "Go to Wiki" on either list
    auto installWikiMenu = [this](QListWidget* list) {
        list->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(list, &QListWidget::customContextMenuRequested,
            this, [this, list](const QPoint& pos) {
                QListWidgetItem* item = list->itemAt(pos);
                if (!item) return;
                const QString tag = item->text();
                QMenu menu;
                QAction* wikiAct = menu.addAction("Go to Wiki");
                if (menu.exec(QCursor::pos()) == wikiAct)
                    emit wikiRequested(tag);
            });
    };
    installWikiMenu(m_tagList);
    installWikiMenu(m_undefinedList);

    auto* leftPanel = new QWidget;
    leftPanel->setObjectName("FacetLeftPanel");
    leftPanel->setFixedWidth(260);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    leftLayout->addWidget(leftHeader);
    leftLayout->addWidget(m_searchEdit);
    leftLayout->addWidget(m_undefinedHeader);
    leftLayout->addWidget(m_undefinedList);
    leftLayout->addWidget(m_countLabel);
    leftLayout->addWidget(m_tagList, 1);

    // ── Right panel — facet assignment editor ─────────────────────────────────
    m_selectedLabel = new QLabel;
    m_selectedLabel->setObjectName("FacetSelectedTag");

    auto* schemaOpenBtn = new QPushButton("↗");
    schemaOpenBtn->setObjectName("SidebarBtn");
    schemaOpenBtn->setFixedSize(20, 20);
    schemaOpenBtn->setCursor(Qt::PointingHandCursor);
    schemaOpenBtn->setToolTip("Open facets.fct in editor");
    connect(schemaOpenBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(utils::BASE_PATH + "/" + utils::FACETS_PATH));
    });

    auto* schemaReloadBtn = new QPushButton("↺");
    schemaReloadBtn->setObjectName("SidebarBtn");
    schemaReloadBtn->setFixedSize(20, 20);
    schemaReloadBtn->setCursor(Qt::PointingHandCursor);
    schemaReloadBtn->setToolTip("Reload facets.fct (does not touch tag definitions)");
    connect(schemaReloadBtn, &QPushButton::clicked,
            this, &FacetEditorPage::schemaReloadRequested);

    auto* selectedRow = new QHBoxLayout;
    selectedRow->setContentsMargins(0, 0, 0, 0);
    selectedRow->setSpacing(4);
    selectedRow->addWidget(m_selectedLabel, 1);
    selectedRow->addWidget(schemaOpenBtn);
    selectedRow->addWidget(schemaReloadBtn);

    m_facetSearchEdit = new QLineEdit;
    m_facetSearchEdit->setObjectName("FacetSearchBar");
    m_facetSearchEdit->setPlaceholderText("filter facets...");
    m_facetSearchEdit->setClearButtonEnabled(true);
    connect(m_facetSearchEdit, &QLineEdit::textChanged,
            this, &FacetEditorPage::applyFacetFilter);

    m_facetsContainer = new QWidget;
    m_facetsLayout    = new QVBoxLayout(m_facetsContainer);
    m_facetsLayout->setContentsMargins(8, 8, 8, 8);
    m_facetsLayout->setSpacing(6);
    m_facetsLayout->addStretch();

    auto* facetsScroll = new QScrollArea;
    facetsScroll->setObjectName("FacetCheckScroll");
    facetsScroll->setWidget(m_facetsContainer);
    facetsScroll->setWidgetResizable(true);
    facetsScroll->setFrameShape(QFrame::NoFrame);
    facetsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    facetsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_saveBtn = new QPushButton("Save definition");
    m_saveBtn->setObjectName("FacetSaveBtn");
    connect(m_saveBtn, &QPushButton::clicked, this, &FacetEditorPage::saveSelected);

    auto* editorWidget = new QWidget;
    auto* editorLayout = new QVBoxLayout(editorWidget);
    editorLayout->setContentsMargins(12, 12, 12, 12);
    editorLayout->setSpacing(8);
    editorLayout->addLayout(selectedRow);
    editorLayout->addWidget(m_facetSearchEdit);
    editorLayout->addWidget(facetsScroll, 1);
    editorLayout->addWidget(m_saveBtn);

    auto* hintLabel = new QLabel("Select a tag from the list\nto assign facets.");
    hintLabel->setObjectName("FacetEditorHint");
    hintLabel->setAlignment(Qt::AlignCenter);

    m_rightStack = new QStackedWidget;
    m_rightStack->addWidget(hintLabel);    // 0
    m_rightStack->addWidget(editorWidget); // 1

    // ── Root layout ───────────────────────────────────────────────────────────
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(leftPanel);
    root->addWidget(m_rightStack, 1);

    reload();
}

// ── Public ────────────────────────────────────────────────────────────────────

void FacetEditorPage::reload()
{
    const QString prevSelected = m_selectedTag;

    m_tagList->clear(); // triggers selectTag("") → clearEditor()

    QList<QString> all = m_model->tagIndex().allTags();
    std::sort(all.begin(), all.end());

    int definedCount = 0;
    for (const QString& tag : all) {
        const bool defined = m_facets->hasFacets(tag);
        if (defined) ++definedCount;

        auto* item = new QListWidgetItem(tag);
        item->setData(Qt::UserRole, defined);
        if (defined)
            item->setForeground(QColor("#3a6a3a"));
        m_tagList->addItem(item);
    }

    m_countLabel->setText(
        QString("  %1 / %2 defined").arg(definedCount).arg(m_tagList->count()));

    if (!prevSelected.isEmpty()) {
        const auto items = m_tagList->findItems(prevSelected, Qt::MatchExactly);
        if (!items.isEmpty()) {
            m_tagList->setCurrentItem(items.first());
            m_tagList->scrollToItem(items.first());
        }
    }
}

void FacetEditorPage::setActiveTagsProvider(std::function<QList<QString>()> provider)
{
    m_activeTagsProvider = std::move(provider);
}

void FacetEditorPage::refreshUndefinedList()
{
    m_undefinedList->clear();

    if (!m_activeTagsProvider) {
        m_undefinedHeader->hide();
        m_undefinedList->hide();
        return;
    }

    const QList<QString> active    = m_activeTagsProvider();
    const QList<QString> undefined = m_facets->undefined(active);

    if (undefined.isEmpty()) {
        m_undefinedHeader->hide();
        m_undefinedList->hide();
        return;
    }

    // Preserve composer's tag order — the user typed them in that order
    for (const QString& tag : undefined)
        m_undefinedList->addItem(new QListWidgetItem(tag));

    m_undefinedHeader->setText(
        QString("UNDEFINED IN COMPOSER  (%1)").arg(undefined.size()));
    m_undefinedHeader->show();
    m_undefinedList->show();
    applyListFilter(m_searchEdit->text());
}

void FacetEditorPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refreshUndefinedList();
}

void FacetEditorPage::applyListFilter(const QString& query)
{
    const QString lower = query.trimmed().toLower();
    auto applyTo = [&](QListWidget* list) {
        for (int i = 0; i < list->count(); ++i) {
            auto* item = list->item(i);
            item->setHidden(!lower.isEmpty() &&
                            !item->text().contains(lower, Qt::CaseInsensitive));
        }
    };
    applyTo(m_tagList);
    applyTo(m_undefinedList);
}

void FacetEditorPage::applyFacetFilter(const QString& query)
{
    const QString lower = query.trimmed().toLower();

    // Match logic per block: empty query → show all. Otherwise, if the
    // category name matches, show every pill in the block; else show only
    // pills whose facet name matches. Hide blocks with zero visible pills.
    for (auto* block : m_facetsContainer->findChildren<QFrame*>("FacetCategoryBlock")) {
        const QString cat = block->property("_categoryName").toString().toLower();
        const bool catMatches = !lower.isEmpty() && cat.contains(lower);

        bool anyVisible = false;
        for (auto* pill : block->findChildren<QPushButton*>("FacetPillBtn")) {
            const bool match = lower.isEmpty()
                            || catMatches
                            || pill->text().toLower().contains(lower);
            pill->setVisible(match);
            if (match) anyVisible = true;
        }
        block->setVisible(anyVisible);
    }
}

void FacetEditorPage::selectTagByName(const QString& tag)
{
    if (tag.isEmpty()) return;
    m_searchEdit->clear();

    const auto items = m_tagList->findItems(tag, Qt::MatchExactly);
    if (!items.isEmpty()) {
        m_tagList->setCurrentItem(items.first());
        m_tagList->scrollToItem(items.first());
    } else {
        // Tag not in model's index yet — add it temporarily
        auto* item = new QListWidgetItem(tag);
        const bool defined = m_facets->hasFacets(tag);
        item->setData(Qt::UserRole, defined);
        if (defined) item->setForeground(QColor("#3a6a3a"));
        m_tagList->addItem(item);
        m_tagList->setCurrentItem(item);
        m_tagList->scrollToItem(item);
    }
}

// ── Private ───────────────────────────────────────────────────────────────────

void FacetEditorPage::selectTag(const QString& tag)
{
    if (tag.isEmpty()) { clearEditor(); return; }

    m_selectedTag = tag;
    m_selectedLabel->setText(tag);

    const QList<QString> existing = m_facets->facetsFor(tag);
    m_saveBtn->setText(m_facets->hasFacets(tag) ? "Update definition" : "Save definition");

    // Clear old checklist
    while (m_facetsLayout->count() > 0) {
        QLayoutItem* item = m_facetsLayout->takeAt(0);
        if (QWidget* w = item->widget()) delete w;
        delete item;
    }

    // Helper: build one category block containing a grid of checkboxes
    auto addBlock = [&](const QString& catName, const QList<QString>& facets) {
        if (facets.isEmpty()) return;

        auto* block = new QFrame;
        block->setObjectName("FacetCategoryBlock");
        block->setAttribute(Qt::WA_StyledBackground);
        block->setProperty("_categoryName", catName);  // for the filter below

        auto* blockLayout = new QVBoxLayout(block);
        blockLayout->setContentsMargins(10, 6, 10, 10);
        blockLayout->setSpacing(6);

        if (!catName.isEmpty()) {
            auto* header = new QLabel(catName.toUpper());
            header->setObjectName("FacetCategoryLabel");
            blockLayout->addWidget(header);
        }

        // Wrap-flowing toggle pills — bigger click target than a checkbox,
        // visually quicker to scan, and shows selected state via :checked QSS.
        auto* pillsHost = new QWidget;
        auto* flow      = new FlowLayout(pillsHost, /*margin*/ 0, /*hSpace*/ 6, /*vSpace*/ 6);

        for (const QString& f : facets) {
            auto* pill = new QPushButton(f);
            pill->setObjectName("FacetPillBtn");
            pill->setCheckable(true);
            pill->setChecked(existing.contains(f));
            pill->setCursor(Qt::PointingHandCursor);
            pill->setFocusPolicy(Qt::NoFocus);
            flow->addWidget(pill);
        }

        blockLayout->addWidget(pillsHost);
        m_facetsLayout->addWidget(block);
    };

    const QList<QString> categories = m_facets->allCategories();
    if (categories.isEmpty()) {
        addBlock({}, m_facets->allFacets());
    } else {
        for (const QString& cat : categories) {
            QList<QString> catFacets;
            for (const QString& f : m_facets->allFacets())
                if (m_facets->categoryFor(f) == cat)
                    catFacets << f;
            addBlock(cat, catFacets);
        }
    }

    m_facetsLayout->addStretch();
    m_rightStack->setCurrentIndex(1);

    m_facetSearchEdit->clear();
}

void FacetEditorPage::saveSelected()
{
    if (m_selectedTag.isEmpty()) return;

    QList<QString> checked;
    for (auto* pill : m_facetsContainer->findChildren<QPushButton*>())
        if (pill->isChecked())
            checked << pill->text();

    // Empty list is valid: clears the tag's definition (FacetIndex removes
    // the entry entirely). Don't return early — that left the on-disk file
    // out of sync with the user's intent to "uncategorize" a tag.
    const bool nowDefined = !checked.isEmpty();

    // If selection came from the undefined list, advance to the next undefined
    // tag after save. For tags selected from the all-tags list (refining an
    // existing definition), stay where we are.
    const bool wasFromUndefined = (m_undefinedList->currentItem() != nullptr);

    m_facets->setDefinition(m_selectedTag, checked);

    const auto items = m_tagList->findItems(m_selectedTag, Qt::MatchExactly);
    for (auto* item : items) {
        item->setData(Qt::UserRole, nowDefined);
        item->setForeground(nowDefined ? QColor("#3a6a3a") : QColor());
    }

    int definedCount = 0;
    for (int i = 0; i < m_tagList->count(); ++i)
        if (m_tagList->item(i)->data(Qt::UserRole).toBool())
            ++definedCount;
    m_countLabel->setText(
        QString("  %1 / %2 defined").arg(definedCount).arg(m_tagList->count()));

    m_saveBtn->setText(nowDefined ? "Update definition" : "Save definition");
    refreshUndefinedList();  // saved tag drops out (or back into) the undefined section
    emit facetsDefined();

    if (wasFromUndefined) {
        if (m_undefinedList->count() > 0)
            m_undefinedList->setCurrentRow(0);  // fires currentTextChanged → selectTag
        else
            clearEditor();
    }
}

void FacetEditorPage::clearEditor()
{
    m_selectedTag.clear();
    m_rightStack->setCurrentIndex(0);
}

} // namespace gui
