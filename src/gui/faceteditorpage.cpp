#include <gui/faceteditorpage.h>
#include <gui/appscrollbar.h>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QStackedWidget>
#include <QFrame>
#include <algorithm>

namespace gui {

FacetEditorPage::FacetEditorPage(
    core::FacetIndex*  facets,
    model::EntryModel* model,
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

    m_countLabel = new QLabel;
    m_countLabel->setObjectName("FacetCountLabel");

    m_tagList = new QListWidget;
    m_tagList->setObjectName("FacetTagList");
    m_tagList->setSortingEnabled(true);
    m_tagList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tagList->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));

    connect(m_tagList, &QListWidget::currentTextChanged,
            this, &FacetEditorPage::selectTag);

    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        const QString lower = text.trimmed().toLower();
        for (int i = 0; i < m_tagList->count(); ++i) {
            auto* item = m_tagList->item(i);
            item->setHidden(!lower.isEmpty() &&
                            !item->text().contains(lower, Qt::CaseInsensitive));
        }
    });

    auto* leftPanel = new QWidget;
    leftPanel->setObjectName("FacetLeftPanel");
    leftPanel->setFixedWidth(260);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    leftLayout->addWidget(leftHeader);
    leftLayout->addWidget(m_searchEdit);
    leftLayout->addWidget(m_countLabel);
    leftLayout->addWidget(m_tagList, 1);

    // ── Right panel — facet assignment editor ─────────────────────────────────
    m_selectedLabel = new QLabel;
    m_selectedLabel->setObjectName("FacetSelectedTag");

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
    editorLayout->addWidget(m_selectedLabel);
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

        auto* blockLayout = new QVBoxLayout(block);
        blockLayout->setContentsMargins(10, 6, 10, 10);
        blockLayout->setSpacing(6);

        if (!catName.isEmpty()) {
            auto* header = new QLabel(catName.toUpper());
            header->setObjectName("FacetCategoryLabel");
            blockLayout->addWidget(header);
        }

        // Checkboxes in a compact grid (3 columns)
        auto* grid = new QWidget;
        auto* gl   = new QGridLayout(grid);
        gl->setContentsMargins(0, 0, 0, 0);
        gl->setHorizontalSpacing(12);
        gl->setVerticalSpacing(2);

        constexpr int Cols = 3;
        int r = 0, c = 0;
        for (const QString& f : facets) {
            auto* cb = new QCheckBox(f);
            cb->setObjectName("FacetCheckBox");
            cb->setChecked(existing.contains(f));
            gl->addWidget(cb, r, c);
            if (++c >= Cols) { c = 0; ++r; }
        }

        blockLayout->addWidget(grid);
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
}

void FacetEditorPage::saveSelected()
{
    if (m_selectedTag.isEmpty()) return;

    QList<QString> checked;
    for (auto* cb : m_facetsContainer->findChildren<QCheckBox*>())
        if (cb->isChecked())
            checked << cb->text();
    if (checked.isEmpty()) return;

    m_facets->setDefinition(m_selectedTag, checked);

    const auto items = m_tagList->findItems(m_selectedTag, Qt::MatchExactly);
    for (auto* item : items) {
        item->setData(Qt::UserRole, true);
        item->setForeground(QColor("#3a6a3a"));
    }

    int definedCount = 0;
    for (int i = 0; i < m_tagList->count(); ++i)
        if (m_tagList->item(i)->data(Qt::UserRole).toBool())
            ++definedCount;
    m_countLabel->setText(
        QString("  %1 / %2 defined").arg(definedCount).arg(m_tagList->count()));

    m_saveBtn->setText("Update definition");
    emit facetsDefined();
}

void FacetEditorPage::clearEditor()
{
    m_selectedTag.clear();
    m_rightStack->setCurrentIndex(0);
}

} // namespace gui
