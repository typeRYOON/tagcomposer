#include <app/facet_editor_page.h>
#include <app/app_data.h>
#include <app/app_scroll_bar.h>
#include <app/dtext.h>
#include <app/flow_layout.h>
#include <app/icons.h>
#include <app/paths.h>
#include <app/tag_preview_fetcher.h>
#include <app/widget_utils.h>
#include <QApplication>
#include <QCursor>
#include <QDesktopServices>
#include <QEvent>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <climits>
#include <cstdlib>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

QLabel* panelHeader(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(u"FacetPanelHeader"_s);
    return label;
}

QListWidget* tagListWidget()
{
    auto* list = new QListWidget;
    list->setObjectName(u"FacetTagList"_s);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setVerticalScrollBar(new AppScrollBar(Qt::Vertical));
    return list;
}

QPushButton* headerButton(const QIcon& icon, const QString& tooltip)
{
    auto* button = new QPushButton;
    button->setObjectName(u"SidebarBtn"_s);
    button->setFixedSize(20, 20);
    button->setIcon(icon);
    button->setIconSize(QSize(14, 14));
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(tooltip);
    return button;
}

constexpr auto kPillName = "FacetPillBtn";

} // namespace

FacetEditorPage::FacetEditorPage(AppData& data, QWidget* parent)
    : QWidget(parent), m_data(&data)
{
    setObjectName(u"FacetEditorPage"_s);
    setAttribute(Qt::WA_StyledBackground, true);

    // ================= Left: the tag list =================
    m_searchEdit = new QLineEdit;
    m_searchEdit->setObjectName(u"FacetSearchBar"_s);
    m_searchEdit->setPlaceholderText(u"filter..."_s);
    m_searchEdit->setClearButtonEnabled(true);

    m_undefinedHeader = panelHeader(QString());
    m_undefinedHeader->hide();

    m_undefinedList = tagListWidget();
    m_undefinedList->setMaximumHeight(160);
    m_undefinedList->hide();

    m_countLabel = new QLabel;
    m_countLabel->setObjectName(u"FacetCountLabel"_s);

    m_tagList = tagListWidget();
    m_tagList->setSortingEnabled(true);

    // Both lists feed selectTag; picking in one clears the other's highlight
    // so the selection is never ambiguous.
    connect(m_tagList, &QListWidget::currentTextChanged, this, [this](const QString& text) {
        if (!text.isEmpty()) m_undefinedList->setCurrentItem(nullptr);
        selectTag(text);
    });
    connect(m_undefinedList, &QListWidget::currentTextChanged, this, [this](const QString& text) {
        if (!text.isEmpty()) m_tagList->setCurrentItem(nullptr);
        selectTag(text);
    });

    connect(m_searchEdit, &QLineEdit::textChanged, this, &FacetEditorPage::applyListFilter);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
        const QString tag = m_searchEdit->text().trimmed();
        if (tag.isEmpty()) return;
        selectTagByName(tag);
        focusFirstPill();
    });

    m_tagList->installEventFilter(this);
    m_undefinedList->installEventFilter(this);

    auto installWikiMenu = [this](QListWidget* list) {
        list->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(list, &QListWidget::customContextMenuRequested, this,
                [this, list](const QPoint& pos) {
                    QListWidgetItem* item = list->itemAt(pos);
                    if (!item) return;

                    const QString tag = item->text();
                    QMenu menu;
                    QAction* wiki = menu.addAction(u"Go to Wiki"_s);
                    if (menu.exec(QCursor::pos()) == wiki) emit wikiRequested(tag);
                });
    };
    installWikiMenu(m_tagList);
    installWikiMenu(m_undefinedList);

    auto* leftPanel = new QWidget;
    leftPanel->setObjectName(u"FacetLeftPanel"_s);
    leftPanel->setFixedWidth(260);

    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    leftLayout->addWidget(panelHeader(u"TAGS"_s));
    leftLayout->addWidget(m_searchEdit);
    leftLayout->addWidget(m_undefinedHeader);
    leftLayout->addWidget(m_undefinedList);
    leftLayout->addWidget(m_countLabel);
    leftLayout->addWidget(m_tagList, 1);

    // ================= Middle: the facet pills =================
    m_selectedLabel = new QLabel;
    m_selectedLabel->setObjectName(u"FacetSelectedTag"_s);
    m_selectedLabel->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_selectedLabel, &QWidget::customContextMenuRequested, this, [this](const QPoint&) {
        if (m_selectedTag.isEmpty()) return;

        QMenu menu;
        QAction* wiki = menu.addAction(u"Go to Wiki"_s);
        if (menu.exec(QCursor::pos()) == wiki) emit wikiRequested(m_selectedTag);
    });

    QPushButton* schemaOpenBtn =
        headerButton(icons::openExternal(), u"Open facets.fct in editor"_s);
    connect(schemaOpenBtn, &QPushButton::clicked, this,
            [this]() { openSystemFile(m_data->dataPath(paths::kFacets)); });

    QPushButton* schemaReloadBtn = headerButton(
        icons::reload(), u"Reload facets.fct (does not touch tag definitions)"_s);
    connect(schemaReloadBtn, &QPushButton::clicked, this, [this]() {
        const QString error = m_data->reloadSchema();
        if (!error.isEmpty()) {
            emit statusMessage(error);
            return;
        }
        // The pill grid is built from the schema, so re-select to rebuild it.
        const QString tag = m_selectedTag;
        if (!tag.isEmpty()) selectTag(tag);
        emit statusMessage(u"facets.fct reloaded"_s);
    });

    auto* selectedRow = new QWidget;
    selectedRow->setObjectName(u"FacetSelectedTagRow"_s);
    selectedRow->setAttribute(Qt::WA_StyledBackground, true);

    auto* selectedRowLayout = new QHBoxLayout(selectedRow);
    selectedRowLayout->setContentsMargins(0, 0, 0, 0);
    selectedRowLayout->setSpacing(4);
    selectedRowLayout->addWidget(m_selectedLabel, 1);
    selectedRowLayout->addWidget(schemaOpenBtn);
    selectedRowLayout->addWidget(schemaReloadBtn);

    m_facetSearchEdit = new QLineEdit;
    m_facetSearchEdit->setObjectName(u"FacetSearchBar"_s);
    m_facetSearchEdit->setPlaceholderText(u"filter facets..."_s);
    m_facetSearchEdit->setClearButtonEnabled(true);
    m_facetSearchEdit->installEventFilter(this);
    connect(m_facetSearchEdit, &QLineEdit::textChanged, this,
            &FacetEditorPage::applyFacetFilter);

    // The active-facet strip: clicking a pill here removes that facet.
    m_activePillsHost = new QWidget;
    m_activePillsHost->setObjectName(u"FacetActivePillsHost"_s);
    m_activePillsFlow = new FlowLayout(m_activePillsHost, 4, 6, 6);
    m_activePillsHost->hide();

    m_facetsContainer = new QWidget;
    m_facetsContainer->installEventFilter(this);
    m_facetsLayout = new QVBoxLayout(m_facetsContainer);
    m_facetsLayout->setContentsMargins(8, 8, 8, 8);
    m_facetsLayout->setSpacing(6);
    m_facetsLayout->addStretch();

    m_facetsScroll = new QScrollArea;
    m_facetsScroll->setObjectName(u"FacetCheckScroll"_s);
    m_facetsScroll->setWidget(m_facetsContainer);
    m_facetsScroll->setWidgetResizable(true);
    m_facetsScroll->setFrameShape(QFrame::NoFrame);
    m_facetsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_facetsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_saveBtn = new QPushButton(u"Save definition"_s);
    m_saveBtn->setObjectName(u"FacetSaveBtn"_s);
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

    // ================= Right: the Danbooru rail =================
    m_preview = new TagPreviewFetcher(this);
    connect(m_preview, &TagPreviewFetcher::loading, this, [this](const QString&) {
        m_previewStatus->show();
        m_previewStatus->setText(QString::fromUtf8("Loading\xE2\x80\xA6"));
    });
    connect(m_preview, &TagPreviewFetcher::wikiBodyReady, this,
            [this](const QString& tag, const QString& body) {
                if (tag != m_selectedTag) return;
                setWikiBody(body);
            });
    connect(m_preview, &TagPreviewFetcher::imageReady, this,
            [this](const QString& tag, const QPixmap& image, int postId) {
                if (tag != m_selectedTag) return;
                m_previewPostId = postId;
                setPreviewPixmap(image);
            });
    connect(m_preview, &TagPreviewFetcher::failed, this,
            [this](const QString& tag, const QString& reason) {
                if (tag != m_selectedTag) return;
                m_previewStatus->show();
                m_previewStatus->setText(reason);
            });

    m_previewPanel = new QWidget;
    m_previewPanel->setObjectName(u"FacetPreviewPanel"_s);
    m_previewPanel->setAttribute(Qt::WA_StyledBackground, true);
    m_previewPanel->setFixedWidth(380);

    m_previewImage = new QLabel;
    m_previewImage->setObjectName(u"FacetPreviewImage"_s);
    m_previewImage->setAlignment(Qt::AlignCenter);
    m_previewImage->setMinimumHeight(380);
    m_previewImage->installEventFilter(this); // click through to the post

    m_previewFade = new QGraphicsOpacityEffect(m_previewImage);
    m_previewFade->setOpacity(1.0);
    m_previewImage->setGraphicsEffect(m_previewFade);

    m_previewFadeAnim = new QPropertyAnimation(m_previewFade, "opacity", this);
    m_previewFadeAnim->setDuration(220);
    m_previewFadeAnim->setEasingCurve(QEasingCurve::InOutSine);

    m_previewStatus = new QLabel;
    m_previewStatus->setObjectName(u"FacetPreviewStatus"_s);
    m_previewStatus->setAlignment(Qt::AlignCenter);
    m_previewStatus->setWordWrap(true);

    // The wiki text comes from the same wiki_pages fetch the preview uses -
    // a reading aid while assigning facets.
    m_wikiHeader = panelHeader(u"WIKI"_s);
    m_wikiHeader->hide();

    m_wikiText = new QTextBrowser;
    m_wikiText->setObjectName(u"FacetWikiText"_s);
    m_wikiText->setFrameShape(QFrame::NoFrame);
    m_wikiText->setOpenLinks(false); // routed below instead
    m_wikiText->setVerticalScrollBar(new AppScrollBar(Qt::Vertical));
    m_wikiText->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_wikiText->hide();

    connect(m_wikiText, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        // The same schemes dtextToHtml emits: [[tag]] stays in the app, post
        // and asset references go to Danbooru.
        if (url.scheme() == "wiki"_L1) {
            emit wikiRequested(url.path());
            return;
        }
        if (url.scheme() == "post"_L1 || url.scheme() == "asset"_L1) {
            const QString kind = url.scheme() == "post"_L1 ? u"posts"_s : u"media_assets"_s;
            QDesktopServices::openUrl(
                QUrl(u"https://danbooru.donmai.us/%1/%2"_s.arg(kind, url.path())));
            return;
        }
        QDesktopServices::openUrl(url);
    });

    auto* previewBody = new QVBoxLayout;
    previewBody->setContentsMargins(12, 8, 12, 12);
    previewBody->setSpacing(8);
    previewBody->addWidget(m_previewImage);
    previewBody->addWidget(m_previewStatus);

    auto* previewLayout = new QVBoxLayout(m_previewPanel);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(8);
    previewLayout->addWidget(panelHeader(u"IMAGE"_s));
    previewLayout->addLayout(previewBody);
    previewLayout->addWidget(m_wikiHeader);
    previewLayout->addWidget(m_wikiText, 1);

    auto* editorOuter = new QWidget;
    auto* editorOuterLayout = new QHBoxLayout(editorOuter);
    editorOuterLayout->setContentsMargins(0, 0, 0, 0);
    editorOuterLayout->setSpacing(0);
    editorOuterLayout->addSpacing(24);
    editorOuterLayout->addWidget(editorWidget, 1);
    editorOuterLayout->addSpacing(24);
    editorOuterLayout->addWidget(m_previewPanel);

    auto* hint = new QLabel(u"Select a tag from the list\nto assign facets."_s);
    hint->setObjectName(u"FacetEditorHint"_s);
    hint->setAlignment(Qt::AlignCenter);

    m_rightStack = new QStackedWidget;
    m_rightStack->addWidget(hint);
    m_rightStack->addWidget(editorOuter);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(leftPanel);
    root->addWidget(m_rightStack, 1);

    reload();
}

void FacetEditorPage::reload()
{
    const QString previous = m_selectedTag;

    m_tagList->clear(); // fires selectTag("") -> clearEditor()

    // The union of every tag in use and every tag with a definition, so a
    // defined tag no entry uses still shows up.
    QSet<QString> seen;
    QStringList all;
    for (const Entry& entry : m_data->entries.all()) {
        for (const EntryImage& image : entry.images) {
            for (const QString& tag : image.tags) {
                if (seen.contains(tag)) continue;
                seen.insert(tag);
                all << tag;
            }
        }
    }
    for (const QString& tag : m_data->defsFile.defs.definedTags()) {
        if (seen.contains(tag)) continue;
        seen.insert(tag);
        all << tag;
    }
    std::sort(all.begin(), all.end());

    int definedCount = 0;
    for (const QString& tag : all) {
        const bool defined = m_data->defsFile.defs.isDefined(tag);
        if (defined) ++definedCount;

        auto* item = new QListWidgetItem(tag);
        item->setData(Qt::UserRole, defined);
        if (defined) item->setForeground(QColor(0x3a, 0x6a, 0x3a));
        m_tagList->addItem(item);
    }

    m_countLabel->setText(u"  %1 / %2 defined"_s.arg(definedCount).arg(m_tagList->count()));

    if (previous.isEmpty()) return;
    const QList<QListWidgetItem*> items = m_tagList->findItems(previous, Qt::MatchExactly);
    if (items.isEmpty()) return;
    m_tagList->setCurrentItem(items.first());
    m_tagList->scrollToItem(items.first());
}

void FacetEditorPage::setActiveTagsProvider(std::function<QStringList()> provider)
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

    const TagFacets& defs = m_data->defsFile.defs;
    const Variables& vars = m_data->varsFile.vars;

    // Mirrors the pipeline's lookup: try the expanded tag, then the stripped
    // one. The stripped form is what shows, so a var-prefixed tag collapses
    // onto the bare tag it would actually define facets for.
    QStringList undefined;
    QSet<QString> seen;
    for (const QString& tag : m_activeTagsProvider()) {
        if (defs.isDefined(vars.expand(tag))) continue;

        QString canonical = tag;
        if (hasVariable(tag)) {
            const QString base = stripVariables(tag);
            if (!base.isEmpty()) {
                if (defs.isDefined(base)) continue;
                canonical = base;
            }
        }
        if (seen.contains(canonical)) continue;
        seen.insert(canonical);
        undefined << canonical;
    }

    if (undefined.isEmpty()) {
        m_undefinedHeader->hide();
        m_undefinedList->hide();
        return;
    }

    // Kept in the composer's order: that is the order they were typed in.
    for (const QString& tag : undefined)
        m_undefinedList->addItem(new QListWidgetItem(tag));

    m_undefinedHeader->setText(u"UNDEFINED IN COMPOSER  (%1)"_s.arg(undefined.size()));
    m_undefinedHeader->show();
    m_undefinedList->show();
    applyListFilter(m_searchEdit->text());
}

void FacetEditorPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refreshUndefinedList();

    // Land on something editable rather than the hint page.
    if (m_undefinedList->isVisible() && m_undefinedList->count() > 0) {
        m_undefinedList->setCurrentRow(0); // fires currentTextChanged
        focusFirstPill();
    }
}

void FacetEditorPage::applyListFilter(const QString& query)
{
    const QString needle = query.trimmed().toLower();

    auto applyTo = [&needle](QListWidget* list) {
        for (int i = 0; i < list->count(); ++i) {
            QListWidgetItem* item = list->item(i);
            item->setHidden(!needle.isEmpty()
                            && !item->text().contains(needle, Qt::CaseInsensitive));
        }
    };
    applyTo(m_tagList);
    applyTo(m_undefinedList);
}

void FacetEditorPage::applyFacetFilter(const QString& query)
{
    const QString needle = query.trimmed().toLower();

    for (QFrame* block : m_facetsContainer->findChildren<QFrame*>(u"FacetCategoryBlock"_s)) {
        const QString category = block->property("_categoryName").toString().toLower();
        const bool categoryMatches = !needle.isEmpty() && category.contains(needle);

        bool anyVisible = false;
        for (QPushButton* pill :
             block->findChildren<QPushButton*>(QString::fromLatin1(kPillName))) {
            const bool match = needle.isEmpty() || categoryMatches
                || pill->text().toLower().contains(needle);
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
    refreshUndefinedList();

    if (m_undefinedList->isVisible()) {
        const QList<QListWidgetItem*> items =
            m_undefinedList->findItems(tag, Qt::MatchExactly);
        if (!items.isEmpty()) {
            m_undefinedList->setCurrentItem(items.first());
            m_undefinedList->scrollToItem(items.first());
            focusFirstPill();
            return;
        }
    }

    const QList<QListWidgetItem*> items = m_tagList->findItems(tag, Qt::MatchExactly);
    if (!items.isEmpty()) {
        m_tagList->setCurrentItem(items.first());
        m_tagList->scrollToItem(items.first());
    } else {
        // Not in the list yet, so add it for this session.
        auto* item = new QListWidgetItem(tag);
        const bool defined = m_data->defsFile.defs.isDefined(tag);
        item->setData(Qt::UserRole, defined);
        if (defined) item->setForeground(QColor(0x3a, 0x6a, 0x3a));
        m_tagList->addItem(item);
        m_tagList->setCurrentItem(item);
        m_tagList->scrollToItem(item);
    }
    focusFirstPill();
}

void FacetEditorPage::selectTag(const QString& tag)
{
    if (tag.isEmpty()) {
        clearEditor();
        return;
    }

    m_selectedTag = tag;
    m_selectedLabel->setText(tag);

    const int category = m_data->danbooru.tagCategory(tag);
    m_selectedLabel->setStyleSheet(
        u"color: %1;"_s.arg(danbooruCategoryColor(category, QColor(0x88, 0x88, 0x88)).name()));

    clearPreview();
    if (category >= 0) m_preview->fetch(tag);

    const TagFacets& defs = m_data->defsFile.defs;
    const QStringList existing = defs.facetsFor(tag);
    m_saveBtn->setText(defs.isDefined(tag) ? u"Update definition"_s : u"Save definition"_s);

    while (m_facetsLayout->count() > 0) {
        QLayoutItem* item = m_facetsLayout->takeAt(0);
        if (QWidget* widget = item->widget()) delete widget;
        delete item;
    }

    // Pills rather than checkboxes: a bigger target, quicker to scan, and
    // their state comes straight from the :checked rule.
    auto addBlock = [&](const QString& categoryName, const QStringList& facets) {
        if (facets.isEmpty()) return;

        auto* block = new QFrame;
        block->setObjectName(u"FacetCategoryBlock"_s);
        block->setAttribute(Qt::WA_StyledBackground);
        block->setProperty("_categoryName", categoryName); // read by the filter

        auto* blockLayout = new QVBoxLayout(block);
        blockLayout->setContentsMargins(10, 6, 10, 10);
        blockLayout->setSpacing(6);

        if (!categoryName.isEmpty()) {
            auto* header = new QLabel(categoryName.toUpper());
            header->setObjectName(u"FacetCategoryLabel"_s);
            blockLayout->addWidget(header);
        }

        auto* pillsHost = new QWidget;
        auto* flow = new FlowLayout(pillsHost, 0, 6, 6);

        for (const QString& facet : facets) {
            auto* pill = new QPushButton(facet);
            pill->setObjectName(QString::fromLatin1(kPillName));
            pill->setCheckable(true);
            pill->setChecked(existing.contains(facet));
            pill->setCursor(Qt::PointingHandCursor);
            pill->setFocusPolicy(Qt::NoFocus);
            flow->addWidget(pill);
        }

        blockLayout->addWidget(pillsHost);
        m_facetsLayout->addWidget(block);
    };

    const FacetSchema& schema = m_data->schema;
    if (schema.categories().isEmpty()) {
        addBlock({}, schema.facets());
    } else {
        for (const QString& category : schema.categories()) {
            QStringList facets;
            for (const QString& facet : schema.facets())
                if (schema.categoryFor(facet) == category) facets << facet;
            addBlock(category, facets);
        }
    }

    m_facetsLayout->addStretch();

    for (QPushButton* pill :
         m_facetsContainer->findChildren<QPushButton*>(QString::fromLatin1(kPillName))) {
        pill->setFocusPolicy(Qt::StrongFocus);
        pill->installEventFilter(this);
        connect(pill, &QPushButton::toggled, this, [this](bool) { refreshActivePills(); });
    }
    for (QWidget* widget : m_facetsContainer->findChildren<QWidget*>())
        if (widget->objectName() != QLatin1StringView(kPillName))
            widget->installEventFilter(this);

    refreshActivePills();
    m_rightStack->setCurrentIndex(1);
    m_facetSearchEdit->clear();
}

void FacetEditorPage::refreshActivePills()
{
    if (!m_activePillsFlow || !m_activePillsHost) return;

    while (QLayoutItem* item = m_activePillsFlow->takeAt(0)) {
        if (QWidget* widget = item->widget()) widget->deleteLater();
        delete item;
    }

    QList<QPushButton*> active;
    for (QPushButton* pill :
         m_facetsContainer->findChildren<QPushButton*>(QString::fromLatin1(kPillName)))
        if (pill->isChecked()) active << pill;

    if (active.isEmpty()) {
        m_activePillsHost->hide();
        return;
    }

    m_activePillsHost->show();
    for (QPushButton* source : active) {
        auto* mini = new QPushButton(source->text());
        mini->setObjectName(QString::fromLatin1(kPillName));
        mini->setCheckable(true);
        mini->setChecked(true);
        mini->setCursor(Qt::PointingHandCursor);
        mini->setFocusPolicy(Qt::NoFocus);

        const QPointer<QPushButton> target(source);
        connect(mini, &QPushButton::clicked, this, [target]() {
            if (target) target->setChecked(false);
        });
        m_activePillsFlow->addWidget(mini);
    }
}

void FacetEditorPage::saveSelected()
{
    if (m_selectedTag.isEmpty()) return;

    QStringList checked;
    for (QPushButton* pill :
         m_facetsContainer->findChildren<QPushButton*>(QString::fromLatin1(kPillName)))
        if (pill->isChecked()) checked << pill->text();

    // An empty list is a valid answer: it clears the definition. Returning
    // early here would leave the file out of step with the intent to
    // un-categorise a tag.
    const bool nowDefined = !checked.isEmpty();

    // A tag picked from the undefined list advances to the next one after a
    // save; a tag picked from the full list stays put, since that is someone
    // refining an existing definition.
    const bool fromUndefined = m_undefinedList->currentItem() != nullptr;

    m_data->defsFile.defs.set(m_selectedTag, checked);
    const QString error = m_data->saveDefinitions();
    if (!error.isEmpty()) emit statusMessage(error);

    for (QListWidgetItem* item : m_tagList->findItems(m_selectedTag, Qt::MatchExactly)) {
        item->setData(Qt::UserRole, nowDefined);
        item->setForeground(nowDefined ? QColor(0x3a, 0x6a, 0x3a) : QColor());
    }

    int definedCount = 0;
    for (int i = 0; i < m_tagList->count(); ++i)
        if (m_tagList->item(i)->data(Qt::UserRole).toBool()) ++definedCount;
    m_countLabel->setText(u"  %1 / %2 defined"_s.arg(definedCount).arg(m_tagList->count()));

    m_saveBtn->setText(nowDefined ? u"Update definition"_s : u"Save definition"_s);
    refreshUndefinedList(); // the saved tag drops out of that section
    emit facetsDefined();

    if (fromUndefined) {
        if (m_undefinedList->count() > 0) {
            m_undefinedList->setCurrentRow(0);
            focusFirstPill();
        } else {
            clearEditor();
            emit composerRequested();
        }
        return;
    }

    QListWidgetItem* current = m_tagList->currentItem();
    if (!current) return;

    int next = m_tagList->row(current) + 1;
    while (next < m_tagList->count() && m_tagList->item(next)->isHidden())
        ++next;
    if (next >= m_tagList->count()) return;

    m_tagList->setCurrentRow(next);
    m_tagList->scrollToItem(m_tagList->item(next));
    focusFirstPill();
}

void FacetEditorPage::focusFirstPill()
{
    for (QPushButton* pill :
         m_facetsContainer->findChildren<QPushButton*>(QString::fromLatin1(kPillName))) {
        if (!pill->isVisible()) continue;
        pill->setFocus(Qt::TabFocusReason);
        if (m_facetsScroll) m_facetsScroll->ensureWidgetVisible(pill, 24, 24);
        return;
    }
}

void FacetEditorPage::clearEditor()
{
    m_selectedTag.clear();
    m_rightStack->setCurrentIndex(0);
    clearPreview();
}

void FacetEditorPage::setWikiBody(const QString& body)
{
    if (!m_wikiText) return;

    const QString dtext = body.trimmed();
    if (dtext.isEmpty()) {
        m_wikiText->clear();
        m_wikiText->hide();
        if (m_wikiHeader) m_wikiHeader->hide();
        return;
    }

    // No post/asset collectors here: this panel cannot resolve thumbnail
    // resources, so those bullets render as links rather than broken images.
    m_wikiText->setHtml(wikiPanelCss() + dtextToHtml(dtext));
    m_wikiText->verticalScrollBar()->setValue(0);
    m_wikiText->show();
    if (m_wikiHeader) m_wikiHeader->show();
}

void FacetEditorPage::clearPreview()
{
    if (m_preview) m_preview->cancel();
    m_previewPostId = -1;

    if (m_previewFadeAnim) m_previewFadeAnim->stop();
    if (m_previewFade) m_previewFade->setOpacity(1.0);

    if (m_previewImage) {
        m_previewImage->clear();
        m_previewImage->hide();
        m_previewImage->setCursor(Qt::ArrowCursor);
    }
    if (m_previewStatus) {
        m_previewStatus->clear();
        m_previewStatus->hide();
    }
    setWikiBody({});
}

void FacetEditorPage::setPreviewPixmap(const QPixmap& image)
{
    if (!m_previewImage) return;

    m_previewStatus->hide();
    m_previewStatus->clear();

    constexpr int maxWidth = 356; // the panel's 380 less its 12px margins
    constexpr int maxHeight = 520;
    m_previewImage->setPixmap(roundedPreview(image, maxWidth, maxHeight));
    m_previewImage->show();
    m_previewImage->setCursor(m_previewPostId > 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);

    if (!m_previewFadeAnim || !m_previewFade) return;
    m_previewFadeAnim->stop();
    m_previewFade->setOpacity(0.0);
    m_previewFadeAnim->setStartValue(0.0);
    m_previewFadeAnim->setEndValue(1.0);
    m_previewFadeAnim->start();
}

bool FacetEditorPage::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_previewImage && event->type() == QEvent::MouseButtonRelease
        && m_previewPostId > 0) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton
            && m_previewImage->rect().contains(mouse->pos())) {
            QDesktopServices::openUrl(
                QUrl(u"https://danbooru.donmai.us/posts/%1"_s.arg(m_previewPostId)));
            return true;
        }
    }

    // Typing in either list goes to the filter box, so the keyboard never has
    // to leave the list to narrow it.
    if (event->type() == QEvent::KeyPress
        && (watched == m_tagList || watched == m_undefinedList)) {
        auto* key = static_cast<QKeyEvent*>(event);
        const int code = key->key();

        if (code == Qt::Key_Right && key->modifiers() == Qt::NoModifier) {
            focusFirstPill();
            return true;
        }

        switch (code) {
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
        default:
            break;
        }

        QKeyEvent forwarded(QEvent::KeyPress, code, key->modifiers(), key->text());
        m_searchEdit->setFocus();
        QApplication::sendEvent(m_searchEdit, &forwarded);
        return true;
    }

    if (event->type() == QEvent::KeyPress && watched == m_facetSearchEdit) {
        auto* key = static_cast<QKeyEvent*>(event);
        const int code = key->key();

        const bool plainArrow = (code == Qt::Key_Left || code == Qt::Key_Right
                                 || code == Qt::Key_Up || code == Qt::Key_Down)
            && key->modifiers() == Qt::NoModifier;
        if (plainArrow) {
            focusFirstPill();
            return true;
        }
        if ((code == Qt::Key_Return || code == Qt::Key_Enter)
            && key->modifiers().testFlag(Qt::ShiftModifier)) {
            m_saveBtn->click();
            return true;
        }
    }

    if (event->type() == QEvent::KeyPress) {
        auto* button = qobject_cast<QPushButton*>(watched);
        if (button && button->objectName() == QLatin1StringView(kPillName)) {
            auto* key = static_cast<QKeyEvent*>(event);
            const int code = key->key();

            if (code == Qt::Key_Left || code == Qt::Key_Right || code == Qt::Key_Up
                || code == Qt::Key_Down) {
                if (QPushButton* next = neighborPill(button, code)) {
                    next->setFocus(Qt::TabFocusReason);
                    if (m_facetsScroll) m_facetsScroll->ensureWidgetVisible(next, 24, 24);
                    return true;
                }
            }

            if (code == Qt::Key_Return || code == Qt::Key_Enter) {
                if (key->modifiers().testFlag(Qt::ShiftModifier))
                    m_saveBtn->click();
                else
                    button->toggle();
                return true;
            }

            if (code == Qt::Key_Escape) {
                QListWidget* source =
                    (m_undefinedList->isVisible() && m_undefinedList->currentItem())
                    ? m_undefinedList
                    : m_tagList;
                source->setFocus();
                return true;
            }

            // Keys the pill handles itself.
            if (code == Qt::Key_Space || code == Qt::Key_Tab || code == Qt::Key_Backtab)
                return false;

            // A bare modifier must not steal focus.
            if (code == Qt::Key_Shift || code == Qt::Key_Control || code == Qt::Key_Alt
                || code == Qt::Key_Meta || code == Qt::Key_AltGr)
                return false;

            // Everything else reaches the filter, so it stays usable from
            // inside the pill grid.
            QKeyEvent forwarded(QEvent::KeyPress, code, key->modifiers(), key->text());
            m_facetSearchEdit->setFocus();
            QApplication::sendEvent(m_facetSearchEdit, &forwarded);
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonPress) {
        auto* widget = qobject_cast<QWidget*>(watched);
        if (widget && widget->objectName() != QLatin1StringView(kPillName)
            && (widget == m_facetsContainer || m_facetsContainer->isAncestorOf(widget))) {
            for (QPushButton* pill :
                 m_facetsContainer->findChildren<QPushButton*>(QString::fromLatin1(kPillName))) {
                if (!pill->isVisible()) continue;
                pill->setFocus(Qt::MouseFocusReason);
                break;
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

QPushButton* FacetEditorPage::neighborPill(QPushButton* current, int key) const
{
    QList<QPushButton*> all;
    for (QPushButton* pill :
         m_facetsContainer->findChildren<QPushButton*>(QString::fromLatin1(kPillName)))
        if (pill->isVisible()) all << pill;
    if (all.isEmpty()) return nullptr;

    const qsizetype index = all.indexOf(current);
    if (index < 0) return nullptr;

    if (key == Qt::Key_Right) return index + 1 < all.size() ? all[index + 1] : all.first();
    if (key == Qt::Key_Left) return index > 0 ? all[index - 1] : all.last();

    // Up and down want the nearest pill on another row, weighted toward the
    // same x so a column reads as a column.
    const QPoint centre = current->mapToGlobal(current->rect().center());
    const int rowGap = current->height() / 2;

    QPushButton* best = nullptr;
    int bestScore = INT_MAX;
    for (QPushButton* pill : all) {
        if (pill == current) continue;

        const QPoint point = pill->mapToGlobal(pill->rect().center());
        const int dy = point.y() - centre.y();
        if (key == Qt::Key_Down && dy <= rowGap) continue;
        if (key == Qt::Key_Up && dy >= -rowGap) continue;

        const int score = std::abs(dy) * 4 + std::abs(point.x() - centre.x());
        if (score >= bestScore) continue;
        bestScore = score;
        best = pill;
    }
    if (!best) best = key == Qt::Key_Down ? all.first() : all.last();
    return best;
}

} // namespace tc
