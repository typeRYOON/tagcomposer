#include <gui/faceteditorpage.h>
#include <gui/widgets/appscrollbar.h>
#include <gui/widgets/composericons.h>
#include <gui/widgets/flowlayout.h>
#include <utils/appconfig.h>
#include <utils/qutils.h>
#include <QApplication>
#include <QCursor>
#include <QDesktopServices>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QNetworkReply>
#include <QPainter>
#include <QPainterPath>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QStackedWidget>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <climits>
#include <cstdlib>

namespace gui {

namespace {
QColor danbooruCategoryColor(int cat)
{
    switch (cat) {
    case 0:
        return {0xb4, 0xc7, 0xd9}; // general
    case 1:
        return {0xf2, 0xac, 0x08}; // artist
    case 3:
        return {0xdd, 0x00, 0xdd}; // copyright
    case 4:
        return {0x00, 0xaa, 0x00}; // character
    case 5:
        return {0xaa, 0xaa, 0xaa}; // meta
    default:
        return {0x88, 0x88, 0x88};
    }
}

// In-app tags use spaces; the Danbooru API expects underscores.
// Parens stay literal (URL-safe in path component).
QString tagToApiSlug(const QString& tag)
{
    QString s = tag.toLower();
    s.replace(' ', '_');
    return s;
}
} // namespace

FacetEditorPage::FacetEditorPage(core::FacetIndex* facets, core::EntryModel* model, QWidget* parent)
    : QWidget(parent), m_facets(facets), m_model(model)
{
    setObjectName("FacetEditorPage");
    setAttribute(Qt::WA_StyledBackground, true);

    // ---- Left panel
    auto* leftHeader = new QLabel("TAGS");
    leftHeader->setObjectName("FacetPanelHeader");

    m_searchEdit = new QLineEdit;
    m_searchEdit->setObjectName("FacetSearchBar");
    m_searchEdit->setPlaceholderText("filter...");
    m_searchEdit->setClearButtonEnabled(true);

    m_undefinedHeader = new QLabel;
    m_undefinedHeader->setObjectName("FacetPanelHeader");
    m_undefinedHeader->hide(); // shown by refreshUndefinedList when non-empty

    m_undefinedList = new QListWidget;
    m_undefinedList->setObjectName("FacetTagList");
    m_undefinedList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_undefinedList->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));
    m_undefinedList->setMaximumHeight(160); // ~6 rows; longer lists scroll
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
    connect(m_tagList, &QListWidget::currentTextChanged, this, [this](const QString& text) {
        if (!text.isEmpty()) m_undefinedList->setCurrentItem(nullptr);
        selectTag(text);
    });
    connect(m_undefinedList, &QListWidget::currentTextChanged, this, [this](const QString& text) {
        if (!text.isEmpty()) m_tagList->setCurrentItem(nullptr);
        selectTag(text);
    });

    connect(m_searchEdit, &QLineEdit::textChanged, this, &FacetEditorPage::applyListFilter);

    // Enter selects the typed tag, or creates a new entry if it doesn't exist.
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
        const QString tag = m_searchEdit->text().trimmed();
        if (tag.isEmpty()) return;
        selectTagByName(tag);
        focusFirstPill();
    });

    m_tagList->installEventFilter(this);
    m_undefinedList->installEventFilter(this);

    // Right-click -> "Go to Wiki" on either list
    auto installWikiMenu = [this](QListWidget* list) {
        list->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(list, &QListWidget::customContextMenuRequested, this,
                [this, list](const QPoint& pos) {
                    QListWidgetItem* item = list->itemAt(pos);
                    if (!item) return;
                    const QString tag = item->text();
                    QMenu menu;
                    QAction* wikiAct = menu.addAction("Go to Wiki");
                    if (menu.exec(QCursor::pos()) == wikiAct) emit wikiRequested(tag);
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

    // ---- Right panel - facet assignment editor
    m_selectedLabel = new QLabel;
    m_selectedLabel->setObjectName("FacetSelectedTag");
    m_selectedLabel->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_selectedLabel, &QWidget::customContextMenuRequested, this, [this](const QPoint&) {
        if (m_selectedTag.isEmpty()) return;
        QMenu menu;
        QAction* wikiAct = menu.addAction("Go to Wiki");
        if (menu.exec(QCursor::pos()) == wikiAct) emit wikiRequested(m_selectedTag);
    });

    auto* schemaOpenBtn = new QPushButton;
    schemaOpenBtn->setObjectName("SidebarBtn");
    schemaOpenBtn->setFixedSize(20, 20);
    schemaOpenBtn->setIcon(gui::icons::openExternal());
    schemaOpenBtn->setIconSize(QSize(14, 14));
    schemaOpenBtn->setCursor(Qt::PointingHandCursor);
    schemaOpenBtn->setToolTip("Open facets.fct in editor");
    connect(schemaOpenBtn, &QPushButton::clicked, this, []() {
        utils::openSystemFile(utils::BASE_PATH + "/" + utils::FACETS_PATH);
    });

    auto* schemaReloadBtn = new QPushButton;
    schemaReloadBtn->setObjectName("SidebarBtn");
    schemaReloadBtn->setFixedSize(20, 20);
    schemaReloadBtn->setIcon(gui::icons::reload());
    schemaReloadBtn->setIconSize(QSize(14, 14));
    schemaReloadBtn->setCursor(Qt::PointingHandCursor);
    schemaReloadBtn->setToolTip("Reload facets.fct (does not touch tag definitions)");
    connect(schemaReloadBtn, &QPushButton::clicked, this, &FacetEditorPage::schemaReloadRequested);

    // Wrap the row in a styled container so the underline runs the full width
    // (under the open/reload buttons too), not just under the label.
    auto* selectedRow = new QWidget;
    selectedRow->setObjectName("FacetSelectedTagRow");
    selectedRow->setAttribute(Qt::WA_StyledBackground, true);
    auto* selectedRowLayout = new QHBoxLayout(selectedRow);
    selectedRowLayout->setContentsMargins(0, 0, 0, 0);
    selectedRowLayout->setSpacing(4);
    selectedRowLayout->addWidget(m_selectedLabel, 1);
    selectedRowLayout->addWidget(schemaOpenBtn);
    selectedRowLayout->addWidget(schemaReloadBtn);

    m_facetSearchEdit = new QLineEdit;
    m_facetSearchEdit->setObjectName("FacetSearchBar");
    m_facetSearchEdit->setPlaceholderText("filter facets...");
    m_facetSearchEdit->setClearButtonEnabled(true);
    m_facetSearchEdit->installEventFilter(this);
    connect(m_facetSearchEdit, &QLineEdit::textChanged, this, &FacetEditorPage::applyFacetFilter);

    // Active-facet strip; click a pill here to remove that facet.
    m_activePillsHost = new QWidget;
    m_activePillsHost->setObjectName("FacetActivePillsHost");
    m_activePillsFlow = new FlowLayout(m_activePillsHost, /*margin*/ 4, /*hSpace*/ 6, /*vSpace*/ 6);
    m_activePillsHost->hide();

    m_facetsContainer = new QWidget;
    m_facetsContainer->installEventFilter(this);
    m_facetsLayout = new QVBoxLayout(m_facetsContainer);
    m_facetsLayout->setContentsMargins(8, 8, 8, 8);
    m_facetsLayout->setSpacing(6);
    m_facetsLayout->addStretch();

    m_facetsScroll = new QScrollArea;
    m_facetsScroll->setObjectName("FacetCheckScroll");
    m_facetsScroll->setWidget(m_facetsContainer);
    m_facetsScroll->setWidgetResizable(true);
    m_facetsScroll->setFrameShape(QFrame::NoFrame);
    m_facetsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_facetsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_saveBtn = new QPushButton("Save definition");
    m_saveBtn->setObjectName("FacetSaveBtn");
    connect(m_saveBtn, &QPushButton::clicked, this, &FacetEditorPage::saveSelected);

    auto* editorWidget = new QWidget;
    editorWidget->setMinimumWidth(200);
    auto* editorLayout = new QVBoxLayout(editorWidget);
    editorLayout->setContentsMargins(12, 12, 12, 12);
    editorLayout->setSpacing(8);
    editorLayout->addWidget(selectedRow);
    editorLayout->addWidget(m_facetSearchEdit);
    editorLayout->addWidget(m_activePillsHost);
    editorLayout->addWidget(m_facetsScroll, 1);
    editorLayout->addWidget(m_saveBtn);

    // ---- Danbooru preview rail (placed inline with the editor below)
    m_nam = new QNetworkAccessManager(this);

    m_previewPanel = new QWidget;
    m_previewPanel->setObjectName("FacetPreviewPanel");
    m_previewPanel->setAttribute(Qt::WA_StyledBackground, true);
    m_previewPanel->setFixedWidth(380);

    auto* previewHeader = new QLabel("IMAGE");
    previewHeader->setObjectName("FacetPanelHeader");

    m_previewImage = new QLabel;
    m_previewImage->setObjectName("FacetPreviewImage");
    m_previewImage->setAlignment(Qt::AlignCenter);
    m_previewImage->setMinimumHeight(380);
    m_previewImage->installEventFilter(this); // for click-through to the post page

    m_previewStatus = new QLabel;
    m_previewStatus->setObjectName("FacetPreviewStatus");
    m_previewStatus->setAlignment(Qt::AlignCenter);
    m_previewStatus->setWordWrap(true);

    auto* previewLayout = new QVBoxLayout(m_previewPanel);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(8);
    previewLayout->addWidget(previewHeader);
    auto* previewBody = new QVBoxLayout;
    previewBody->setContentsMargins(12, 8, 12, 12);
    previewBody->setSpacing(8);
    previewBody->addStretch();
    previewBody->addWidget(m_previewImage);
    previewBody->addWidget(m_previewStatus);
    previewBody->addStretch();
    previewLayout->addLayout(previewBody);

    // Only fixed pieces are the 24px gaps on either side of the editor;
    // the editor takes all remaining width between TAGS and the preview panel.
    auto* editorOuter = new QWidget;
    auto* editorOuterLayout = new QHBoxLayout(editorOuter);
    editorOuterLayout->setContentsMargins(0, 0, 0, 0);
    editorOuterLayout->setSpacing(0);
    editorOuterLayout->addSpacing(24);
    editorOuterLayout->addWidget(editorWidget, 1);
    editorOuterLayout->addSpacing(24);
    editorOuterLayout->addWidget(m_previewPanel);

    auto* hintLabel = new QLabel("Select a tag from the list\nto assign facets.");
    hintLabel->setObjectName("FacetEditorHint");
    hintLabel->setAlignment(Qt::AlignCenter);

    m_rightStack = new QStackedWidget;
    m_rightStack->addWidget(hintLabel);   // 0
    m_rightStack->addWidget(editorOuter); // 1

    // ---- Root layout
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(leftPanel);
    root->addWidget(m_rightStack, 1);

    reload();
}

// ---- Public

void FacetEditorPage::reload()
{
    const QString prevSelected = m_selectedTag;

    m_tagList->clear(); // triggers selectTag("") -> clearEditor()

    // Union of entry tags + tags defined in tag_definitions.fct - so tags
    // with definitions but no entry usage still appear in the list.
    QSet<QString> seen;
    QList<QString> all;
    for (const QString& tag : m_model->tagIndex().allTags())
        if (!seen.contains(tag)) { seen.insert(tag); all << tag; }
    for (const QString& tag : m_facets->allDefinedTags())
        if (!seen.contains(tag)) { seen.insert(tag); all << tag; }
    std::sort(all.begin(), all.end());

    int definedCount = 0;
    for (const QString& tag : all) {
        const bool defined = m_facets->hasFacets(tag);
        if (defined) ++definedCount;

        auto* item = new QListWidgetItem(tag);
        item->setData(Qt::UserRole, defined);
        if (defined) item->setForeground(QColor("#3a6a3a"));
        m_tagList->addItem(item);
    }

    m_countLabel->setText(QString("  %1 / %2 defined").arg(definedCount).arg(m_tagList->count()));

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

    const QList<QString> active = m_activeTagsProvider();

    // Mirror PromptPipeline lookup (try expanded, then stripped). Display the
    // stripped form so var-prefixed tags collapse onto the bare tag they'd
    // actually define facets for.
    QList<QString> undefined;
    QSet<QString> seen;
    for (const QString& tag : active) {
        const QString expanded = m_varIndex ? m_varIndex->expand(tag) : tag;
        if (m_facets->hasFacets(expanded)) continue;

        QString canonical = tag;
        if (core::VariableIndex::hasVariable(tag)) {
            const QString base = core::VariableIndex::stripVariables(tag);
            if (!base.isEmpty()) {
                if (m_facets->hasFacets(base)) continue;
                canonical = base;
            }
        }
        if (!seen.contains(canonical)) {
            seen.insert(canonical);
            undefined << canonical;
        }
    }

    if (undefined.isEmpty()) {
        m_undefinedHeader->hide();
        m_undefinedList->hide();
        return;
    }

    // Preserve composer's tag order - the user typed them in that order
    for (const QString& tag : undefined)
        m_undefinedList->addItem(new QListWidgetItem(tag));

    m_undefinedHeader->setText(QString("UNDEFINED IN COMPOSER  (%1)").arg(undefined.size()));
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
            item->setHidden(!lower.isEmpty() && !item->text().contains(lower, Qt::CaseInsensitive));
        }
    };
    applyTo(m_tagList);
    applyTo(m_undefinedList);
}

void FacetEditorPage::applyFacetFilter(const QString& query)
{
    const QString lower = query.trimmed().toLower();

    // Match logic per block: empty query -> show all. Otherwise, if the
    // category name matches, show every pill in the block; else show only
    // pills whose facet name matches. Hide blocks with zero visible pills.
    for (auto* block : m_facetsContainer->findChildren<QFrame*>("FacetCategoryBlock")) {
        const QString cat = block->property("_categoryName").toString().toLower();
        const bool catMatches = !lower.isEmpty() && cat.contains(lower);

        bool anyVisible = false;
        for (auto* pill : block->findChildren<QPushButton*>("FacetPillBtn")) {
            const bool match =
                lower.isEmpty() || catMatches || pill->text().toLower().contains(lower);
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
    }
    else {
        // Tag not in model's index yet - add it temporarily
        auto* item = new QListWidgetItem(tag);
        const bool defined = m_facets->hasFacets(tag);
        item->setData(Qt::UserRole, defined);
        if (defined) item->setForeground(QColor("#3a6a3a"));
        m_tagList->addItem(item);
        m_tagList->setCurrentItem(item);
        m_tagList->scrollToItem(item);
    }
}

// ---- Private

void FacetEditorPage::selectTag(const QString& tag)
{
    if (tag.isEmpty()) {
        clearEditor();
        return;
    }

    m_selectedTag = tag;
    m_selectedLabel->setText(tag);

    // Match the entry-panel tag list: color the header by danbooru category
    // so the selected tag's type is recognisable at a glance.
    const int cat = m_danbooruIndex ? m_danbooruIndex->tagCategory(tag) : -1;
    const QColor col = danbooruCategoryColor(cat);
    m_selectedLabel->setStyleSheet(QString("color: %1;").arg(col.name()));

    // Drive the right-rail Danbooru preview. cat == -1 means the tag isn't
    // known to Danbooru, so don't waste a request.
    if (cat >= 0)
        fetchPreview(tag);
    else
        clearPreview();

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
        block->setProperty("_categoryName", catName); // for the filter below

        auto* blockLayout = new QVBoxLayout(block);
        blockLayout->setContentsMargins(10, 6, 10, 10);
        blockLayout->setSpacing(6);

        if (!catName.isEmpty()) {
            auto* header = new QLabel(catName.toUpper());
            header->setObjectName("FacetCategoryLabel");
            blockLayout->addWidget(header);
        }

        // Wrap-flowing toggle pills - bigger click target than a checkbox,
        // visually quicker to scan, and shows selected state via :checked QSS.
        auto* pillsHost = new QWidget;
        auto* flow = new FlowLayout(pillsHost, /*margin*/ 0, /*hSpace*/ 6, /*vSpace*/ 6);

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
    }
    else {
        for (const QString& cat : categories) {
            QList<QString> catFacets;
            for (const QString& f : m_facets->allFacets())
                if (m_facets->categoryFor(f) == cat) catFacets << f;
            addBlock(cat, catFacets);
        }
    }

    m_facetsLayout->addStretch();

    for (auto* pill : m_facetsContainer->findChildren<QPushButton*>("FacetPillBtn")) {
        pill->setFocusPolicy(Qt::StrongFocus);
        pill->installEventFilter(this);
        connect(pill, &QPushButton::toggled, this, [this](bool) { refreshActivePills(); });
    }
    // Catch clicks on non-pill regions (block frame, label, gaps) so the
    // first pill takes focus and arrow-nav works without clicking a pill.
    for (auto* w : m_facetsContainer->findChildren<QWidget*>())
        if (w->objectName() != "FacetPillBtn") w->installEventFilter(this);
    refreshActivePills();

    m_rightStack->setCurrentIndex(1);

    m_facetSearchEdit->clear();
}

void FacetEditorPage::refreshActivePills()
{
    if (!m_activePillsFlow || !m_activePillsHost) return;

    while (QLayoutItem* item = m_activePillsFlow->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }

    QList<QPushButton*> active;
    for (auto* pill : m_facetsContainer->findChildren<QPushButton*>("FacetPillBtn"))
        if (pill->isChecked()) active << pill;

    if (active.isEmpty()) {
        m_activePillsHost->hide();
        return;
    }

    m_activePillsHost->show();
    for (QPushButton* main : active) {
        auto* mini = new QPushButton(main->text());
        mini->setObjectName("FacetPillBtn");
        mini->setCheckable(true);
        mini->setChecked(true);
        mini->setCursor(Qt::PointingHandCursor);
        mini->setFocusPolicy(Qt::NoFocus);
        // Click removes the facet by unchecking the matching pill below;
        // the toggled signal there triggers another refresh of this strip.
        QPointer<QPushButton> mainPtr(main);
        connect(mini, &QPushButton::clicked, this,
                [mainPtr]() { if (mainPtr) mainPtr->setChecked(false); });
        m_activePillsFlow->addWidget(mini);
    }
}

void FacetEditorPage::saveSelected()
{
    if (m_selectedTag.isEmpty()) return;

    QList<QString> checked;
    for (auto* pill : m_facetsContainer->findChildren<QPushButton*>())
        if (pill->isChecked()) checked << pill->text();

    // Empty list is valid: clears the tag's definition (FacetIndex removes
    // the entry entirely). Don't return early - that left the on-disk file
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
        if (m_tagList->item(i)->data(Qt::UserRole).toBool()) ++definedCount;
    m_countLabel->setText(QString("  %1 / %2 defined").arg(definedCount).arg(m_tagList->count()));

    m_saveBtn->setText(nowDefined ? "Update definition" : "Save definition");
    refreshUndefinedList(); // saved tag drops out (or back into) the undefined section
    emit facetsDefined();

    if (wasFromUndefined) {
        if (m_undefinedList->count() > 0) {
            m_undefinedList->setCurrentRow(0); // fires currentTextChanged -> selectTag
            focusFirstPill();
        }
        else {
            clearEditor();
        }
    }
    else if (auto* cur = m_tagList->currentItem()) {
        int next = m_tagList->row(cur) + 1;
        while (next < m_tagList->count() && m_tagList->item(next)->isHidden())
            ++next;
        if (next < m_tagList->count()) {
            m_tagList->setCurrentRow(next);
            m_tagList->scrollToItem(m_tagList->item(next));
            focusFirstPill();
        }
    }
}

void FacetEditorPage::focusFirstPill()
{
    for (auto* p : m_facetsContainer->findChildren<QPushButton*>("FacetPillBtn")) {
        if (p->isVisible()) {
            p->setFocus(Qt::TabFocusReason);
            if (m_facetsScroll) m_facetsScroll->ensureWidgetVisible(p, 24, 24);
            return;
        }
    }
}

void FacetEditorPage::clearEditor()
{
    m_selectedTag.clear();
    m_rightStack->setCurrentIndex(0);
    clearPreview();
}

// ---- Danbooru preview

void FacetEditorPage::clearPreview()
{
    m_previewPostId = -1;
    if (m_previewImage) {
        m_previewImage->clear();
        m_previewImage->hide();
        m_previewImage->setCursor(Qt::ArrowCursor);
    }
    if (m_previewStatus) {
        m_previewStatus->clear();
        m_previewStatus->hide();
    }
}

void FacetEditorPage::setPreviewPixmap(const QPixmap& pix)
{
    if (!m_previewImage) return;
    m_previewStatus->hide();
    m_previewStatus->clear();

    constexpr int maxW = 356; // panel inner width (380 - 12*2 margins)
    constexpr int maxH = 520;
    constexpr qreal kRad = 6.0;

    const QPixmap scaled = pix.scaled(maxW, maxH, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // QSS border-radius on QLabel doesn't clip the pixmap content - paint into
    // a transparent canvas with a rounded clip path so the corners are actually
    // rounded on the image itself.
    QPixmap rounded(scaled.size());
    rounded.fill(Qt::transparent);
    {
        QPainter p(&rounded);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        QPainterPath path;
        path.addRoundedRect(QRectF(rounded.rect()), kRad, kRad);
        p.setClipPath(path);
        p.drawPixmap(0, 0, scaled);
    }

    m_previewImage->setPixmap(rounded);
    m_previewImage->show();
    m_previewImage->setCursor(m_previewPostId > 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

bool FacetEditorPage::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == m_previewImage && ev->type() == QEvent::MouseButtonRelease && m_previewPostId > 0) {
        auto* me = static_cast<QMouseEvent*>(ev);
        if (me->button() == Qt::LeftButton && m_previewImage->rect().contains(me->pos())) {
            QDesktopServices::openUrl(
                QUrl(QString("https://danbooru.donmai.us/posts/%1").arg(m_previewPostId)));
            return true;
        }
    }

    if (ev->type() == QEvent::KeyPress &&
        (obj == m_tagList || obj == m_undefinedList)) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        const int key = ke->key();

        if (key == Qt::Key_Right && ke->modifiers() == Qt::NoModifier) {
            focusFirstPill();
            return true;
        }

        switch (key) {
        case Qt::Key_Left:
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
        case Qt::Key_Home:
        case Qt::Key_End:
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
        case Qt::Key_Escape:
        case Qt::Key_Shift:
        case Qt::Key_Control:
        case Qt::Key_Alt:
        case Qt::Key_Meta:
        case Qt::Key_AltGr:
            return false;
        }

        QKeyEvent fwd(QEvent::KeyPress, key, ke->modifiers(), ke->text());
        m_searchEdit->setFocus();
        QApplication::sendEvent(m_searchEdit, &fwd);
        return true;
    }

    if (ev->type() == QEvent::KeyPress && obj == m_facetSearchEdit) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        const int key = ke->key();
        const bool plainArrow = (key == Qt::Key_Left || key == Qt::Key_Right ||
                                 key == Qt::Key_Up || key == Qt::Key_Down) &&
                                ke->modifiers() == Qt::NoModifier;
        if (plainArrow) {
            focusFirstPill();
            return true;
        }
        if ((key == Qt::Key_Return || key == Qt::Key_Enter) &&
            ke->modifiers().testFlag(Qt::ShiftModifier)) {
            m_saveBtn->click();
            return true;
        }
    }

    if (ev->type() == QEvent::KeyPress) {
        auto* btn = qobject_cast<QPushButton*>(obj);
        if (btn && btn->objectName() == "FacetPillBtn") {
            auto* ke = static_cast<QKeyEvent*>(ev);
            const int key = ke->key();

            if (key == Qt::Key_Left || key == Qt::Key_Right ||
                key == Qt::Key_Up || key == Qt::Key_Down) {
                if (QPushButton* next = neighborPill(btn, key)) {
                    next->setFocus(Qt::TabFocusReason);
                    if (m_facetsScroll) m_facetsScroll->ensureWidgetVisible(next, 24, 24);
                    return true;
                }
            }

            if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                if (ke->modifiers().testFlag(Qt::ShiftModifier))
                    m_saveBtn->click();
                else
                    btn->toggle();
                return true;
            }

            if (key == Qt::Key_Escape) {
                QListWidget* src = (m_undefinedList->isVisible() &&
                                    m_undefinedList->currentItem())
                                       ? m_undefinedList
                                       : m_tagList;
                src->setFocus();
                return true;
            }

            // Pill-handled keys; let default processing run.
            if (key == Qt::Key_Space || key == Qt::Key_Tab || key == Qt::Key_Backtab)
                return false;

            // Bare modifier presses must not steal focus.
            if (key == Qt::Key_Shift || key == Qt::Key_Control ||
                key == Qt::Key_Alt || key == Qt::Key_Meta || key == Qt::Key_AltGr)
                return false;

            // Everything else (typing, Backspace, Ctrl+A/C/V, Home/End, etc.)
            // routes to the filter so the search box stays usable from the pills.
            QKeyEvent fwd(QEvent::KeyPress, key, ke->modifiers(), ke->text());
            m_facetSearchEdit->setFocus();
            QApplication::sendEvent(m_facetSearchEdit, &fwd);
            return true;
        }
    }

    if (ev->type() == QEvent::MouseButtonPress) {
        auto* w = qobject_cast<QWidget*>(obj);
        if (w && w->objectName() != "FacetPillBtn" &&
            (w == m_facetsContainer || m_facetsContainer->isAncestorOf(w))) {
            for (auto* p : m_facetsContainer->findChildren<QPushButton*>("FacetPillBtn")) {
                if (p->isVisible()) {
                    p->setFocus(Qt::MouseFocusReason);
                    break;
                }
            }
        }
    }

    return QWidget::eventFilter(obj, ev);
}

QPushButton* FacetEditorPage::neighborPill(QPushButton* current, int key) const
{
    QList<QPushButton*> all;
    for (auto* p : m_facetsContainer->findChildren<QPushButton*>("FacetPillBtn"))
        if (p->isVisible()) all << p;
    if (all.isEmpty()) return nullptr;

    const int idx = all.indexOf(current);
    if (idx < 0) return nullptr;

    if (key == Qt::Key_Right) return idx + 1 < all.size() ? all[idx + 1] : all.first();
    if (key == Qt::Key_Left)  return idx > 0 ? all[idx - 1] : all.last();

    // Up/Down: nearest pill on a different row, weighted toward same X.
    const QPoint cur = current->mapToGlobal(current->rect().center());
    const int rowGap = current->height() / 2;
    QPushButton* best = nullptr;
    int bestScore = INT_MAX;
    for (auto* p : all) {
        if (p == current) continue;
        const QPoint pt = p->mapToGlobal(p->rect().center());
        const int dy = pt.y() - cur.y();
        if (key == Qt::Key_Down && dy <= rowGap) continue;
        if (key == Qt::Key_Up   && dy >= -rowGap) continue;
        const int score = std::abs(dy) * 4 + std::abs(pt.x() - cur.x());
        if (score < bestScore) { bestScore = score; best = p; }
    }
    if (!best) best = (key == Qt::Key_Down) ? all.first() : all.last();
    return best;
}

void FacetEditorPage::fetchPreview(const QString& tag)
{
    clearPreview();

    if (m_previewCache.contains(tag)) {
        m_previewPostId = m_previewPostIds.value(tag, -1);
        setPreviewPixmap(m_previewCache.value(tag));
        return;
    }

    m_previewStatus->show();
    m_previewStatus->setText("Loading…");

    // Step 1: try the tag's wiki page and look for the first !post #N. Matches
    // what TagWikiPage surfaces in its inline gallery, so the previews stay
    // consistent between the two pages.
    const QString slug = tagToApiSlug(tag);
    const QByteArray encoded = QUrl::toPercentEncoding(slug);
    QUrl url(
        QString("https://danbooru.donmai.us/wiki_pages/%1.json").arg(QString::fromLatin1(encoded)));

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "TagComposer/1.0");
    req.setRawHeader("Accept", "application/json");

    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, tag, reply]() {
        reply->deleteLater();
        if (tag != m_selectedTag) return; // user moved on

        if (reply->error() != QNetworkReply::NoError) {
            // 404 (no wiki) or any network failure -> fall through to a posts search.
            fetchFirstPostByTag(tag);
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            fetchFirstPostByTag(tag);
            return;
        }

        const QString body = doc.object().value("body").toString();
        static const QRegularExpression postRe(R"(!post\s+#(\d+))");
        const auto m = postRe.match(body);
        if (m.hasMatch())
            fetchPostById(tag, m.captured(1).toInt());
        else
            fetchFirstPostByTag(tag);
    });
}

void FacetEditorPage::fetchPostById(const QString& tag, int postId)
{
    QUrl url(QString("https://danbooru.donmai.us/posts/%1.json").arg(postId));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "TagComposer/1.0");
    req.setRawHeader("Accept", "application/json");

    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, tag, postId, reply]() {
        reply->deleteLater();
        if (tag != m_selectedTag) return;

        if (reply->error() != QNetworkReply::NoError) {
            fetchFirstPostByTag(tag);
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            fetchFirstPostByTag(tag);
            return;
        }

        const QJsonObject post = doc.object();
        QString imgUrl = post.value("large_file_url").toString();
        if (imgUrl.isEmpty()) imgUrl = post.value("preview_file_url").toString();
        if (imgUrl.isEmpty()) {
            fetchFirstPostByTag(tag);
            return;
        }
        m_previewPostId = postId;
        m_previewPostIds[tag] = postId;
        fetchPreviewImage(tag, imgUrl);
    });
}

void FacetEditorPage::fetchFirstPostByTag(const QString& tag)
{
    const QString slug = tagToApiSlug(tag);
    QUrl url("https://danbooru.donmai.us/posts.json");
    QUrlQuery q;
    q.addQueryItem("tags", slug);
    q.addQueryItem("limit", "1");
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "TagComposer/1.0");
    req.setRawHeader("Accept", "application/json");

    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, tag, reply]() {
        reply->deleteLater();
        if (tag != m_selectedTag) return;

        if (reply->error() != QNetworkReply::NoError) {
            m_previewStatus->show();
            m_previewStatus->setText("(no preview)");
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isArray() || doc.array().isEmpty()) {
            m_previewStatus->show();
            m_previewStatus->setText("(no posts)");
            return;
        }
        const QJsonObject post = doc.array().first().toObject();
        QString imgUrl = post.value("large_file_url").toString();
        if (imgUrl.isEmpty()) imgUrl = post.value("preview_file_url").toString();
        if (imgUrl.isEmpty()) {
            m_previewStatus->show();
            m_previewStatus->setText("(no preview)");
            return;
        }
        const int postId = post.value("id").toInt();
        m_previewPostId = postId;
        m_previewPostIds[tag] = postId;
        fetchPreviewImage(tag, imgUrl);
    });
}

void FacetEditorPage::fetchPreviewImage(const QString& tag, const QString& imageUrl)
{
    QNetworkRequest req((QUrl(imageUrl)));
    req.setHeader(QNetworkRequest::UserAgentHeader, "TagComposer/1.0");

    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, tag, reply]() {
        reply->deleteLater();
        if (tag != m_selectedTag) return;

        if (reply->error() != QNetworkReply::NoError) {
            m_previewStatus->show();
            m_previewStatus->setText("(image fetch failed)");
            return;
        }
        QPixmap pix;
        if (!pix.loadFromData(reply->readAll()) || pix.isNull()) {
            m_previewStatus->show();
            m_previewStatus->setText("(image decode failed)");
            return;
        }
        m_previewCache[tag] = pix;
        setPreviewPixmap(pix);
    });
}

} // namespace gui
