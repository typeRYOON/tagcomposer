#include <app/workflow_edit_page.h>
#include <app/app_data.h>
#include <app/app_scroll_bar.h>
#include <app/clip_editor_dialog.h>
#include <app/icons.h>
#include <app/paths.h>
#include <app/workflow_input_cache.h>
#include <core/entry_search.h>
#include <core/entry_store.h>
#include <core/workflow_io.h>
#include <QButtonGroup>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QImage>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSet>
#include <QSpinBox>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <climits>
#include <functional>

using namespace Qt::StringLiterals;

namespace {

// Centred rectangle scaled to a w:h ratio; the latent picker's preview.
class RatioPreview : public QWidget {
public:
    explicit RatioPreview(QWidget* parent = nullptr) : QWidget(parent)
    {
        setFixedSize(80, 80);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    void setRatio(int w, int h)
    {
        m_w = w;
        m_h = h;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor(0x0a, 0x0a, 0x0a));
        p.setPen(QColor(0x1e, 0x1e, 0x1e));
        p.drawRect(rect().adjusted(0, 0, -1, -1));

        if (m_w <= 0 || m_h <= 0) return;

        constexpr int pad = 8;
        const double availW = width() - 2.0 * pad;
        const double availH = height() - 2.0 * pad;

        double w = availW;
        double h = availH;
        if (double(m_w) / m_h > availW / availH)
            h = w * double(m_h) / m_w;
        else
            w = h * double(m_w) / m_h;

        const QRectF box(pad + (availW - w) / 2.0, pad + (availH - h) / 2.0, w, h);
        p.fillRect(box, QColor(0x1a, 0x2e, 0x1a));
        p.setPen(QColor(0x33, 0x66, 0x33));
        p.drawRect(box);
    }

private:
    int m_w = 0;
    int m_h = 0;
};

// The thumbnail on an Image card; also its drop target.
class DropImageLabel : public QLabel {
    Q_OBJECT

public:
    explicit DropImageLabel(QWidget* parent = nullptr) : QLabel(parent)
    {
        setAcceptDrops(true);
    }

signals:
    void filePathDropped(const QString& path);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (hasImageUrl(event->mimeData())) event->acceptProposedAction();
    }

    void dragMoveEvent(QDragMoveEvent* event) override
    {
        if (hasImageUrl(event->mimeData())) event->acceptProposedAction();
    }

    void dropEvent(QDropEvent* event) override
    {
        for (const QUrl& url : event->mimeData()->urls()) {
            if (!url.isLocalFile()) continue;
            const QString path = url.toLocalFile();
            if (!looksLikeImage(path)) continue;
            emit filePathDropped(path);
            event->acceptProposedAction();
            return;
        }
    }

private:
    static bool looksLikeImage(const QString& path)
    {
        static const QStringList suffixes = {u".png"_s,  u".jpg"_s,  u".jpeg"_s, u".bmp"_s,
                                             u".webp"_s, u".tif"_s, u".tiff"_s};
        const QString lower = path.toLower();
        for (const QString& suffix : suffixes)
            if (lower.endsWith(suffix)) return true;
        return false;
    }

    static bool hasImageUrl(const QMimeData* mime)
    {
        if (!mime->hasUrls()) return false;
        for (const QUrl& url : mime->urls())
            if (url.isLocalFile() && looksLikeImage(url.toLocalFile())) return true;
        return false;
    }
};

} // namespace

namespace tc {
namespace {

constexpr int kHeaderHeight = 50;

QWidget* columnHeader(QHBoxLayout** layoutOut)
{
    auto* header = new QWidget;
    header->setObjectName(u"WfEditHeader"_s);
    header->setAttribute(Qt::WA_StyledBackground, true);
    header->setFixedHeight(kHeaderHeight);

    auto* layout = new QHBoxLayout(header);
    layout->setContentsMargins(20, 12, 20, 12);
    layout->setSpacing(16);
    *layoutOut = layout;
    return header;
}

QLabel* columnTitle(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(u"WfEditTitle"_s);
    return label;
}

QLabel* fieldLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(u"WfFieldLabel"_s);
    return label;
}

QLineEdit* valueEdit(const QString& text = {})
{
    auto* edit = new QLineEdit(text);
    edit->setObjectName(u"WfValueEdit"_s);
    return edit;
}

QPushButton* smallButton(const QString& text)
{
    auto* button = new QPushButton(text);
    button->setObjectName(u"WfBrowseBtn"_s);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QListWidget* fileList()
{
    auto* list = new QListWidget;
    list->setObjectName(u"WfFileList"_s);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setVerticalScrollBar(new AppScrollBar(Qt::Vertical));
    return list;
}

QScrollArea* columnScroll(QWidget* body)
{
    auto* scroll = new QScrollArea;
    scroll->setObjectName(u"WfEditScroll"_s);
    scroll->setWidget(body);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBar(new AppScrollBar(Qt::Vertical));
    return scroll;
}

// Bolds and tints the selected row.
void markSelected(QListWidget* list, const std::function<bool(QListWidgetItem*)>& isSelected)
{
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem* item = list->item(i);
        const bool selected = isSelected(item);
        QFont font = item->font();
        font.setBold(selected);
        item->setFont(font);
        item->setForeground(QColor(selected ? 0x5a9a5a : 0x666666));
    }
}

// Drops everything but the trailing stretch.
void clearCards(QVBoxLayout* layout)
{
    while (layout->count() > 1) {
        QLayoutItem* item = layout->takeAt(0);
        if (QWidget* widget = item->widget()) widget->deleteLater();
        delete item;
    }
}

QString typeDisplayName(const WorkflowVarValue& value)
{
    return std::visit(
        [](auto&& v) -> QString {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, SeedVar>) return u"Seed"_s;
            else if constexpr (std::is_same_v<T, StringVar>) return u"String"_s;
            else if constexpr (std::is_same_v<T, IntVar>) return u"Integer"_s;
            else if constexpr (std::is_same_v<T, FloatVar>) return u"Float"_s;
            else if constexpr (std::is_same_v<T, DirSearchVar>) return u"Dir Search"_s;
            else if constexpr (std::is_same_v<T, LatentSizeVar>) return u"Latent Size"_s;
            else if constexpr (std::is_same_v<T, ImageVar>) return u"Image"_s;
            else return u"Wildcard"_s;
        },
        value);
}

} // namespace

WorkflowEditPage::WorkflowEditPage(AppData& data, EntryStore& entries, EntrySearch& search,
                                   WorkflowInputCache& cache, QWidget* parent)
    : QWidget(parent), m_data(&data), m_entries(&entries), m_search(&search), m_cache(&cache)
{
    setObjectName(u"WorkflowEditPage"_s);
    setAttribute(Qt::WA_StyledBackground, true);

    // ---- Left: the selected workflow's variables
    QHBoxLayout* leftHeaderLayout = nullptr;
    QWidget* leftHeader = columnHeader(&leftHeaderLayout);

    m_titleLabel = new QLabel(u"No workflow selected"_s);
    m_titleLabel->setObjectName(u"WfEditSubtitle"_s);

    auto* openWfBtn = new QPushButton;
    openWfBtn->setObjectName(u"SidebarBtn"_s);
    openWfBtn->setFixedSize(24, 24);
    openWfBtn->setIcon(icons::openExternal());
    openWfBtn->setIconSize(QSize(14, 14));
    openWfBtn->setCursor(Qt::PointingHandCursor);
    openWfBtn->setToolTip(u"Open the selected workflow JSON in your editor"_s);
    connect(openWfBtn, &QPushButton::clicked, this, [this]() {
        const Workflow* workflow = selectedWorkflow();
        if (!workflow || workflow->path.isEmpty()) return;
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_data->workflowPath(*workflow)));
    });

    m_addBtn = new QPushButton(u"+ Add Variable"_s);
    m_addBtn->setObjectName(u"WfAddBtn"_s);
    m_addBtn->setCursor(Qt::PointingHandCursor);

    leftHeaderLayout->addWidget(columnTitle(u"WORKFLOW VARIABLES"_s));
    leftHeaderLayout->addWidget(m_titleLabel, 1);
    leftHeaderLayout->addWidget(openWfBtn);
    leftHeaderLayout->addWidget(m_addBtn);

    auto* varContainer = new QWidget;
    m_varLayout = new QVBoxLayout(varContainer);
    m_varLayout->setContentsMargins(20, 16, 20, 20);
    m_varLayout->setSpacing(10);
    m_varLayout->addStretch();

    auto* leftPanel = new QWidget;
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    leftLayout->addWidget(leftHeader);
    leftLayout->addWidget(columnScroll(varContainer), 1);

    // ---- Middle: the active LoRA stack
    QHBoxLayout* middleHeaderLayout = nullptr;
    QWidget* middleHeader = columnHeader(&middleHeaderLayout);

    m_loraSubtitle = new QLabel(u"0 active"_s);
    m_loraSubtitle->setObjectName(u"WfEditSubtitle"_s);

    middleHeaderLayout->addWidget(columnTitle(u"LORA STACK"_s));
    middleHeaderLayout->addWidget(m_loraSubtitle, 1);

    auto* loraContainer = new QWidget;
    m_loraLayout = new QVBoxLayout(loraContainer);
    m_loraLayout->setContentsMargins(20, 16, 20, 20);
    m_loraLayout->setSpacing(10);
    m_loraLayout->addStretch();

    auto* middlePanel = new QWidget;
    auto* middleLayout = new QVBoxLayout(middlePanel);
    middleLayout->setContentsMargins(0, 0, 0, 0);
    middleLayout->setSpacing(0);
    middleLayout->addWidget(middleHeader);
    middleLayout->addWidget(columnScroll(loraContainer), 1);

    // ---- Right: batch
    QHBoxLayout* rightHeaderLayout = nullptr;
    QWidget* rightHeader = columnHeader(&rightHeaderLayout);
    rightHeaderLayout->addWidget(columnTitle(u"BATCH"_s));
    rightHeaderLayout->addStretch();

    auto* batchBody = new QWidget;
    auto* batchLayout = new QVBoxLayout(batchBody);
    batchLayout->setContentsMargins(20, 16, 20, 20);
    batchLayout->setSpacing(8);

    auto* batchHint = new QLabel(u"Run the current composer prompt across every entry matching "
                                 "the query - same syntax as the entry viewer's search bar."_s);
    batchHint->setObjectName(u"WfEmptyHint"_s);
    batchHint->setWordWrap(true);
    batchLayout->addWidget(batchHint);

    QLineEdit* batchQueryEdit = valueEdit();
    batchQueryEdit->setPlaceholderText(u"entry query, e.g. \"kantai collection, -nsfw\""_s);
    batchLayout->addWidget(batchQueryEdit);

    m_batchCountLabel = new QLabel(u"0 entries"_s);
    m_batchCountLabel->setObjectName(u"WfEditSubtitle"_s);
    batchLayout->addWidget(m_batchCountLabel);

    m_batchResultsList = fileList();
    m_batchResultsList->setSelectionMode(QAbstractItemView::NoSelection);
    m_batchResultsList->setFocusPolicy(Qt::NoFocus);
    batchLayout->addWidget(m_batchResultsList, 1);

    auto* batchRunBtn = new QPushButton(u" Run Batch"_s);
    batchRunBtn->setObjectName(u"WfAddBtn"_s);
    batchRunBtn->setIcon(icons::play(14, QColor(0x66, 0xaa, 0x66)));
    batchRunBtn->setIconSize(QSize(12, 12));
    batchRunBtn->setCursor(Qt::PointingHandCursor);

    m_batchStatus = new QLabel;
    m_batchStatus->setObjectName(u"WfEditSubtitle"_s);
    m_batchStatus->setWordWrap(true);

    auto* batchBtnRow = new QHBoxLayout;
    batchBtnRow->setSpacing(10);
    batchBtnRow->addWidget(batchRunBtn);
    batchBtnRow->addWidget(m_batchStatus, 1);
    batchLayout->addLayout(batchBtnRow);

    connect(batchRunBtn, &QPushButton::clicked, this, [this, batchQueryEdit]() {
        emit batchRunRequested(batchQueryEdit->text().trimmed());
    });
    connect(batchQueryEdit, &QLineEdit::returnPressed, this, [this, batchQueryEdit]() {
        emit batchRunRequested(batchQueryEdit->text().trimmed());
    });

    // Debounced preview of the query's matches.
    m_batchQueryDebounce = new QTimer(this);
    m_batchQueryDebounce->setSingleShot(true);
    m_batchQueryDebounce->setInterval(120);
    connect(batchQueryEdit, &QLineEdit::textChanged, m_batchQueryDebounce,
            qOverload<>(&QTimer::start));
    connect(m_batchQueryDebounce, &QTimer::timeout, this, [this, batchQueryEdit]() {
        m_batchResultsList->clear();

        const QStringList matched = m_search->find(batchQueryEdit->text().trimmed());
        m_batchCountLabel->setText(u"%1 entr%2 matched"_s.arg(matched.size()).arg(
            matched.size() == 1 ? u"y"_s : u"ies"_s));

        for (const QString& uuid : matched) {
            const Entry* entry = m_entries->find(uuid);
            if (!entry) continue;
            m_batchResultsList->addItem(entry->title.isEmpty() ? entry->uuid : entry->title);
        }
    });

    auto* rightPanel = new QWidget;
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    rightLayout->addWidget(rightHeader);
    rightLayout->addWidget(batchBody, 1);

    // ---- Three equal columns
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(leftPanel, 1);
    root->addWidget(middlePanel, 1);
    root->addWidget(rightPanel, 1);

    // Default placeholders show the expected token style.
    connect(m_addBtn, &QPushButton::clicked, this, [this]() {
        QMenu menu(this);
        menu.addAction(u"Seed"_s, this,
                       [this]() { addVariable(SeedVar{}, u"__seed__"_s); });
        menu.addAction(u"String"_s, this,
                       [this]() { addVariable(StringVar{}, u"__string__"_s); });
        menu.addAction(u"Integer"_s, this, [this]() { addVariable(IntVar{}, u"__int__"_s); });
        menu.addAction(u"Float"_s, this, [this]() { addVariable(FloatVar{}, u"__float__"_s); });
        menu.addAction(u"Dir Search"_s, this,
                       [this]() { addVariable(DirSearchVar{}, u"__dir__"_s); });
        menu.addAction(u"Latent Size"_s, this, [this]() {
            addVariable(LatentSizeVar{0, 0, u"__latentw__"_s, u"__latenth__"_s}, QString());
        });
        menu.addAction(u"Image"_s, this, [this]() { addVariable(ImageVar{}, u"__image__"_s); });
        menu.addAction(u"Wildcard"_s, this,
                       [this]() { addVariable(WildcardVar{}, QString()); });
        menu.exec(m_addBtn->mapToGlobal(QPoint(0, m_addBtn->height())));
    });

    rebuildVarList();
    rebuildLoraList();
}

Workflow* WorkflowEditPage::selectedWorkflow() const
{
    const int index = m_data->workflows.selectedIndex;
    if (index < 0 || index >= m_data->workflows.workflows.size()) return nullptr;
    return &m_data->workflows.workflows[index];
}

QList<WorkflowVar>* WorkflowEditPage::variables() const
{
    Workflow* workflow = selectedWorkflow();
    return workflow ? &workflow->vars : nullptr;
}

void WorkflowEditPage::save()
{
    const QString error = m_data->saveWorkflows();
    if (!error.isEmpty()) emit statusMessage(error);
}

void WorkflowEditPage::refresh()
{
    const Workflow* workflow = selectedWorkflow();
    m_titleLabel->setText(workflow ? workflow->name : u"No workflow selected"_s);
    rebuildVarList();
    rebuildLoraList();
    if (m_batchQueryDebounce) m_batchQueryDebounce->start();
}

void WorkflowEditPage::setBatchResult(const QString& message)
{
    if (m_batchStatus) m_batchStatus->setText(message);
}

void WorkflowEditPage::setActiveLoraStack(const QList<Lora>& stack)
{
    m_loraStack = stack;
    rebuildLoraList();
}

void WorkflowEditPage::addVariable(const WorkflowVarValue& value, const QString& placeholder)
{
    QList<WorkflowVar>* vars = variables();
    if (!vars) {
        emit statusMessage(u"Select a workflow in the composer first"_s);
        return;
    }

    *vars << WorkflowVar{placeholder, value};
    save();
    rebuildVarList();
}

void WorkflowEditPage::removeVariable(int index)
{
    QList<WorkflowVar>* vars = variables();
    if (!vars || index < 0 || index >= vars->size()) return;

    vars->removeAt(index);
    save();
    rebuildVarList();
}

void WorkflowEditPage::rebuildVarList()
{
    clearCards(m_varLayout);

    const QList<WorkflowVar>* vars = variables();
    if (!vars || vars->isEmpty()) {
        auto* hint = new QLabel(vars
                                    ? u"No variables defined.\nClick \"+ Add Variable\" to "
                                      "create one."_s
                                    : u"No workflow selected.\nPick one in the composer's "
                                      "WORKFLOWS list."_s);
        hint->setObjectName(u"WfEmptyHint"_s);
        hint->setAlignment(Qt::AlignCenter);
        m_varLayout->insertWidget(0, hint);
        return;
    }

    for (int i = 0; i < int(vars->size()); ++i)
        m_varLayout->insertWidget(i, makeVarCard(i));
}

QFrame* WorkflowEditPage::makeVarCard(int index)
{
    const WorkflowVar& var = variables()->at(index);

    auto* card = new QFrame;
    card->setObjectName(u"WfVarCard"_s);
    card->setAttribute(Qt::WA_StyledBackground, true);
    // Spare height goes to the trailing stretch.
    card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(12, 10, 12, 12);
    cardLayout->setSpacing(8);

    // ---- Header: token name, type badge, remove
    auto* headerRow = new QHBoxLayout;
    headerRow->setSpacing(6);

    auto* placeholderEdit = new QLineEdit(var.placeholder);
    placeholderEdit->setObjectName(u"WfPlaceholderEdit"_s);

    // Wildcard and latent-size vars have no single token; the field is a label.
    const bool labelOnly = std::holds_alternative<WildcardVar>(var.value)
        || std::holds_alternative<LatentSizeVar>(var.value);
    placeholderEdit->setPlaceholderText(labelOnly ? u"Name (label only)"_s : u"__token__"_s);

    connect(placeholderEdit, &QLineEdit::editingFinished, this, [this, index, placeholderEdit]() {
        QList<WorkflowVar>* vars = variables();
        if (!vars || index >= vars->size()) return;
        (*vars)[index].placeholder = placeholderEdit->text().trimmed();
        save();
    });

    auto* typeLabel = new QLabel(typeDisplayName(var.value));
    typeLabel->setObjectName(u"WfTypeLabel"_s);

    auto* removeBtn = new QPushButton;
    removeBtn->setObjectName(u"WfRemoveBtn"_s);
    removeBtn->setFixedSize(20, 20);
    removeBtn->setCursor(Qt::PointingHandCursor);
    icons::applyStates(removeBtn, icons::close, 10, QColor(0x3a, 0x3a, 0x3a),
                       QColor(0xcc, 0x33, 0x33));
    connect(removeBtn, &QPushButton::clicked, this, [this, index]() { removeVariable(index); });

    headerRow->addWidget(placeholderEdit, 1);
    headerRow->addWidget(typeLabel);
    headerRow->addWidget(removeBtn);
    cardLayout->addLayout(headerRow);

    // ---- Body, one branch per alternative
    if (const auto* seed = std::get_if<SeedVar>(&var.value)) {
        auto* behaviourRow = new QHBoxLayout;
        behaviourRow->setSpacing(10);

        auto* group = new QButtonGroup(card);
        auto addRadio = [&](const QString& text, SeedBehavior behavior) {
            auto* radio = new QRadioButton(text);
            radio->setObjectName(u"WfRadio"_s);
            radio->setChecked(seed->behavior == behavior);
            group->addButton(radio, int(behavior));
            behaviourRow->addWidget(radio);
        };
        addRadio(u"Fixed"_s, SeedBehavior::Fixed);
        addRadio(u"Increment"_s, SeedBehavior::Increment);
        addRadio(u"Randomize"_s, SeedBehavior::Randomize);
        behaviourRow->addStretch();
        cardLayout->addLayout(behaviourRow);

        connect(group, &QButtonGroup::idClicked, this, [this, index](int id) {
            QList<WorkflowVar>* vars = variables();
            if (!vars || index >= vars->size()) return;
            if (auto* value = std::get_if<SeedVar>(&(*vars)[index].value))
                value->behavior = SeedBehavior(id);
            save();
        });

        // A line edit rather than a spin box: seeds run past int range.
        QLineEdit* seedEdit = valueEdit(QString::number(seed->value));
        connect(seedEdit, &QLineEdit::editingFinished, this, [this, index, seedEdit]() {
            bool ok = false;
            const qint64 parsed = seedEdit->text().toLongLong(&ok);
            if (!ok) return;

            QList<WorkflowVar>* vars = variables();
            if (!vars || index >= vars->size()) return;
            if (auto* value = std::get_if<SeedVar>(&(*vars)[index].value)) value->value = parsed;
            save();
        });

        auto* row = new QHBoxLayout;
        row->addWidget(fieldLabel(u"Value:"_s));
        row->addWidget(seedEdit, 1);
        cardLayout->addLayout(row);
    }
    else if (const auto* text = std::get_if<StringVar>(&var.value)) {
        QLineEdit* edit = valueEdit(text->value);
        edit->setPlaceholderText(u"Replacement text"_s);
        connect(edit, &QLineEdit::editingFinished, this, [this, index, edit]() {
            QList<WorkflowVar>* vars = variables();
            if (!vars || index >= vars->size()) return;
            if (auto* value = std::get_if<StringVar>(&(*vars)[index].value))
                value->value = edit->text();
            save();
        });
        cardLayout->addWidget(edit);
    }
    else if (const auto* number = std::get_if<IntVar>(&var.value)) {
        auto* spin = new QSpinBox;
        spin->setObjectName(u"WfSpinBox"_s);
        spin->setRange(INT_MIN, INT_MAX);
        spin->setValue(number->value);
        connect(spin, &QSpinBox::valueChanged, this, [this, index](int v) {
            QList<WorkflowVar>* vars = variables();
            if (!vars || index >= vars->size()) return;
            if (auto* value = std::get_if<IntVar>(&(*vars)[index].value)) value->value = v;
            save();
        });

        auto* row = new QHBoxLayout;
        row->addWidget(fieldLabel(u"Value:"_s));
        row->addWidget(spin, 1);
        cardLayout->addLayout(row);
    }
    else if (const auto* number = std::get_if<FloatVar>(&var.value)) {
        auto* spin = new QDoubleSpinBox;
        spin->setObjectName(u"WfSpinBox"_s);
        spin->setDecimals(4);
        spin->setRange(-1e9, 1e9);
        spin->setSingleStep(0.1);
        spin->setValue(number->value);
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, index](double v) {
            QList<WorkflowVar>* vars = variables();
            if (!vars || index >= vars->size()) return;
            if (auto* value = std::get_if<FloatVar>(&(*vars)[index].value)) value->value = v;
            save();
        });

        auto* row = new QHBoxLayout;
        row->addWidget(fieldLabel(u"Value:"_s));
        row->addWidget(spin, 1);
        cardLayout->addLayout(row);
    }
    else if (const auto* dir = std::get_if<DirSearchVar>(&var.value)) {
        QLineEdit* dirEdit = valueEdit(dir->searchDir);
        dirEdit->setPlaceholderText(u"Search directory..."_s);
        QPushButton* browseBtn = smallButton(u"Browse"_s);

        auto* dirRow = new QHBoxLayout;
        dirRow->setSpacing(6);
        dirRow->addWidget(dirEdit, 1);
        dirRow->addWidget(browseBtn);
        cardLayout->addLayout(dirRow);

        QLineEdit* extEdit = valueEdit(dir->extensionFilter);
        extEdit->setPlaceholderText(
            u"Ext. whitelist: .safetensors, .ckpt  (empty = all except .sha256)"_s);
        cardLayout->addWidget(extEdit);

        // Not persisted.
        QLineEdit* filterEdit = valueEdit();
        filterEdit->setPlaceholderText(u"Filter by name..."_s);
        cardLayout->addWidget(filterEdit);

        QListWidget* list = fileList();
        list->setFixedHeight(200);
        cardLayout->addWidget(list);

        auto scanDir = [this, index, dirEdit, extEdit, filterEdit, list]() {
            list->clear();

            const QString root = dirEdit->text().trimmed();
            if (root.isEmpty()) return;
            if (QDir(root).isRoot()) return; // never walk a whole drive

            QSet<QString> whitelist;
            const QString extRaw = extEdit->text().trimmed();
            if (!extRaw.isEmpty()) {
                for (const QString& piece : extRaw.split(u',')) {
                    const QString ext = piece.trimmed().toLower();
                    if (ext.isEmpty()) continue;
                    whitelist.insert(ext.startsWith(u'.') ? ext : u"."_s + ext);
                }
            }

            const QList<WorkflowVar>* vars = variables();
            QString selected;
            if (vars && index < vars->size())
                if (const auto* value = std::get_if<DirSearchVar>(&vars->at(index).value))
                    selected = value->selectedFile;

            const QString filter = filterEdit->text().trimmed();
            const QDir base(root);

            QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
            int shown = 0;
            while (it.hasNext() && shown < 500) {
                it.next();
                const QString ext = u"."_s + QFileInfo(it.fileName()).suffix().toLower();

                if (whitelist.isEmpty()) {
                    if (ext == ".sha256"_L1) continue; // the one default exclusion
                } else if (!whitelist.contains(ext)) {
                    continue;
                }

                if (!filter.isEmpty() && !it.fileName().contains(filter, Qt::CaseInsensitive))
                    continue;

                auto* item = new QListWidgetItem(base.relativeFilePath(it.filePath()));
                item->setData(Qt::UserRole, it.filePath());
                item->setToolTip(it.filePath());
                if (it.filePath() == selected) {
                    QFont font = item->font();
                    font.setBold(true);
                    item->setFont(font);
                    item->setForeground(QColor(0x5a, 0x9a, 0x5a));
                }
                list->addItem(item);
                ++shown;
            }
        };

        connect(browseBtn, &QPushButton::clicked, this, [this, index, dirEdit, scanDir]() {
            const QString dir =
                QFileDialog::getExistingDirectory(this, u"Select Directory"_s, dirEdit->text());
            if (dir.isEmpty()) return;
            dirEdit->setText(dir);

            QList<WorkflowVar>* vars = variables();
            if (vars && index < vars->size())
                if (auto* value = std::get_if<DirSearchVar>(&(*vars)[index].value)) {
                    value->searchDir = dir;
                    save();
                }
            scanDir();
        });

        connect(dirEdit, &QLineEdit::editingFinished, this, [this, index, dirEdit, scanDir]() {
            QList<WorkflowVar>* vars = variables();
            if (vars && index < vars->size())
                if (auto* value = std::get_if<DirSearchVar>(&(*vars)[index].value)) {
                    value->searchDir = dirEdit->text().trimmed();
                    save();
                }
            scanDir();
        });

        connect(extEdit, &QLineEdit::editingFinished, this, [this, index, extEdit, scanDir]() {
            QList<WorkflowVar>* vars = variables();
            if (vars && index < vars->size())
                if (auto* value = std::get_if<DirSearchVar>(&(*vars)[index].value)) {
                    value->extensionFilter = extEdit->text().trimmed();
                    save();
                }
            scanDir();
        });

        connect(filterEdit, &QLineEdit::textChanged, this,
                [scanDir](const QString&) { scanDir(); });

        connect(list, &QListWidget::itemClicked, this,
                [this, index, list](QListWidgetItem* item) {
                    QList<WorkflowVar>* vars = variables();
                    if (!vars || index >= vars->size()) return;

                    const QString path = item->data(Qt::UserRole).toString();
                    if (auto* value = std::get_if<DirSearchVar>(&(*vars)[index].value))
                        value->selectedFile = path;
                    save();

                    markSelected(list, [&path](QListWidgetItem* row) {
                        return row->data(Qt::UserRole).toString() == path;
                    });
                });

        if (!dir->searchDir.isEmpty()) scanDir();
    }
    else if (const auto* latent = std::get_if<LatentSizeVar>(&var.value)) {
        const QList<LatentSizeEntry> sizes =
            readLatentSizes(m_data->dataPath(paths::kLatentSizes));

        QListWidget* list = fileList();
        list->setFixedHeight(150);

        auto* preview = new RatioPreview;

        if (sizes.isEmpty()) {
            auto* hint = new QListWidgetItem(
                u"File not found: %1"_s.arg(QString::fromLatin1(paths::kLatentSizes)));
            hint->setFlags(Qt::NoItemFlags);
            QFont font = hint->font();
            font.setItalic(true);
            hint->setFont(font);
            list->addItem(hint);
        } else {
            for (const LatentSizeEntry& size : sizes) {
                auto* item = new QListWidgetItem(size.label);
                item->setData(Qt::UserRole, size.width);
                item->setData(Qt::UserRole + 1, size.height);
                if (size.width == latent->width && size.height == latent->height) {
                    QFont font = item->font();
                    font.setBold(true);
                    item->setFont(font);
                    item->setForeground(QColor(0x5a, 0x9a, 0x5a));
                    preview->setRatio(size.width, size.height);
                }
                list->addItem(item);
            }
        }

        connect(list, &QListWidget::itemClicked, this,
                [this, index, list, preview](QListWidgetItem* item) {
                    QList<WorkflowVar>* vars = variables();
                    if (!vars || index >= vars->size()) return;

                    const int w = item->data(Qt::UserRole).toInt();
                    const int h = item->data(Qt::UserRole + 1).toInt();
                    if (w <= 0 || h <= 0) return;

                    if (auto* value = std::get_if<LatentSizeVar>(&(*vars)[index].value)) {
                        value->width = w;
                        value->height = h;
                    }
                    save();
                    preview->setRatio(w, h);

                    markSelected(list, [w, h](QListWidgetItem* row) {
                        return row->data(Qt::UserRole).toInt() == w
                            && row->data(Qt::UserRole + 1).toInt() == h;
                    });
                });

        auto* bodyRow = new QHBoxLayout;
        bodyRow->setSpacing(8);
        bodyRow->setAlignment(Qt::AlignTop);
        bodyRow->addWidget(list, 1);
        bodyRow->addWidget(preview, 0, Qt::AlignTop);
        cardLayout->addLayout(bodyRow);

        // Width and height are substituted as raw integers.
        auto tokenRow = [&](const QString& label, const QString& current,
                            std::function<void(const QString&)> commit) {
            QLineEdit* edit = valueEdit(current);
            edit->setPlaceholderText(u"__token__"_s);
            connect(edit, &QLineEdit::editingFinished, this,
                    [edit, commit]() { commit(edit->text().trimmed()); });

            auto* row = new QHBoxLayout;
            row->addWidget(fieldLabel(label));
            row->addWidget(edit, 1);
            cardLayout->addLayout(row);
        };

        tokenRow(u"Width token:"_s, latent->widthToken, [this, index](const QString& token) {
            QList<WorkflowVar>* vars = variables();
            if (!vars || index >= vars->size()) return;
            if (auto* value = std::get_if<LatentSizeVar>(&(*vars)[index].value))
                value->widthToken = token;
            save();
        });
        tokenRow(u"Height token:"_s, latent->heightToken, [this, index](const QString& token) {
            QList<WorkflowVar>* vars = variables();
            if (!vars || index >= vars->size()) return;
            if (auto* value = std::get_if<LatentSizeVar>(&(*vars)[index].value))
                value->heightToken = token;
            save();
        });
    }
    else if (const auto* image = std::get_if<ImageVar>(&var.value)) {
        auto* thumb = new DropImageLabel;
        thumb->setObjectName(u"WfImageThumb"_s);
        thumb->setFixedSize(96, 96);
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setFrameShape(QFrame::StyledPanel);
        thumb->setToolTip(QString::fromUtf8("Drop an image file here, or use Browse\xE2\x80\xA6"));

        auto* nameLabel = new QLabel;
        nameLabel->setObjectName(u"WfFieldLabel"_s);
        nameLabel->setWordWrap(true);

        auto showImage = [this, index, thumb, nameLabel](const QString& uuid) {
            if (uuid.isEmpty() || !m_cache->has(uuid)) {
                thumb->clear();
                thumb->setText(u"(no image)"_s);
                nameLabel->setText(uuid.isEmpty() ? u"No image selected"_s
                                                  : u"Missing: %1"_s.arg(uuid));
                return;
            }

            const QPixmap pixmap(m_cache->localPath(uuid));
            if (pixmap.isNull())
                thumb->setText(u"(broken)"_s);
            else
                thumb->setPixmap(
                    pixmap.scaled(thumb->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

            const WorkflowInput record = m_cache->get(uuid);
            QString name = record.displayName.isEmpty() ? uuid : record.displayName;

            // Note live edits in the label.
            const QList<WorkflowVar>* vars = variables();
            if (vars && index < vars->size())
                if (const auto* value = std::get_if<ImageVar>(&vars->at(index).value))
                    if (value->edits.enabled) {
                        const QRect r = value->edits.cropRect;
                        name += (value->edits.trimToCrop ? u"  -  cropped %1x%2"_s
                                                         : u"  -  mask %1x%2"_s)
                                    .arg(r.width())
                                    .arg(r.height());
                    }
            nameLabel->setText(name);
        };
        showImage(image->imageUuid);

        QPushButton* browseBtn =
            smallButton(QString::fromUtf8("Browse\xE2\x80\xA6"));
        QPushButton* editBtn = smallButton(QString::fromUtf8("Edit\xE2\x80\xA6"));
        QPushButton* clearBtn = smallButton(u"Clear"_s);

        editBtn->setEnabled(!image->imageUuid.isEmpty() && m_cache->has(image->imageUuid));

        connect(editBtn, &QPushButton::clicked, this, [this, index, showImage]() {
            QList<WorkflowVar>* vars = variables();
            if (!vars || index >= vars->size()) return;

            auto* value = std::get_if<ImageVar>(&(*vars)[index].value);
            if (!value || value->imageUuid.isEmpty() || !m_cache->has(value->imageUuid)) return;

            const QImage source(m_cache->localPath(value->imageUuid));
            if (source.isNull()) {
                emit statusMessage(u"Could not read the cached image"_s);
                return;
            }

            const QString previousMask = value->edits.maskId;

            ClipEditorDialog dialog(source, value->edits, m_cache, this);
            if (dialog.exec() != QDialog::Accepted) return;

            // Re-read: the dialog pumped events.
            vars = variables();
            if (!vars || index >= vars->size()) return;
            value = std::get_if<ImageVar>(&(*vars)[index].value);
            if (!value) return;

            value->edits = dialog.result();

            // Masks get fresh ids on save; drop the orphaned one.
            if (!previousMask.isEmpty() && previousMask != value->edits.maskId)
                m_cache->removeMask(previousMask);

            save();
            showImage(value->imageUuid);
        });

        // A new source drops the edits and their mask file.
        auto resetEdits = [this, index]() {
            QList<WorkflowVar>* vars = variables();
            if (!vars || index >= vars->size()) return;

            auto* value = std::get_if<ImageVar>(&(*vars)[index].value);
            if (!value) return;

            const QString oldMask = value->edits.maskId;
            value->edits = ImageEdits{};
            if (!oldMask.isEmpty()) m_cache->removeMask(oldMask);
        };

        auto setImage = [this, index, showImage, resetEdits](const QString& sourcePath) {
            QList<WorkflowVar>* vars = variables();
            if (!vars || index >= vars->size()) return;

            const QString uuid = m_cache->importFromFile(sourcePath);
            if (uuid.isEmpty()) {
                emit statusMessage(u"Could not read %1"_s.arg(sourcePath));
                return;
            }

            resetEdits();
            if (auto* value = std::get_if<ImageVar>(&(*vars)[index].value))
                value->imageUuid = uuid;
            save();
            showImage(uuid);
        };

        connect(browseBtn, &QPushButton::clicked, this, [this, setImage]() {
            const QString source = QFileDialog::getOpenFileName(
                this, u"Select image"_s, QString(),
                u"Images (*.png *.jpg *.jpeg *.bmp *.webp *.tiff)"_s);
            if (source.isEmpty()) return;
            setImage(source);
        });

        connect(thumb, &DropImageLabel::filePathDropped, this,
                [setImage](const QString& path) { setImage(path); });

        connect(clearBtn, &QPushButton::clicked, this, [this, index, showImage, resetEdits]() {
            QList<WorkflowVar>* vars = variables();
            if (!vars || index >= vars->size()) return;

            resetEdits();
            if (auto* value = std::get_if<ImageVar>(&(*vars)[index].value))
                value->imageUuid.clear();
            save();
            showImage(QString());
        });

        auto* btnRow = new QHBoxLayout;
        btnRow->setSpacing(4);
        btnRow->addWidget(browseBtn);
        btnRow->addWidget(editBtn);
        btnRow->addWidget(clearBtn);
        btnRow->addStretch();

        auto* infoColumn = new QVBoxLayout;
        infoColumn->setSpacing(4);
        infoColumn->addWidget(nameLabel);
        infoColumn->addLayout(btnRow);
        infoColumn->addStretch();

        auto* bodyRow = new QHBoxLayout;
        bodyRow->setSpacing(8);
        bodyRow->setAlignment(Qt::AlignTop);
        bodyRow->addWidget(thumb, 0, Qt::AlignTop);
        bodyRow->addLayout(infoColumn, 1);
        cardLayout->addLayout(bodyRow);
    }
    else if (const auto* wildcard = std::get_if<WildcardVar>(&var.value)) {
        auto* hint = new QLabel(
            u"One slot per line. A random line is picked for each prompt run and merged into "
            "the positive prompt - rules and replacement vars apply. Commas split a line into "
            "multiple tags."_s);
        hint->setObjectName(u"WfFieldLabel"_s);
        hint->setWordWrap(true);
        cardLayout->addWidget(hint);

        auto* edit = new QPlainTextEdit(wildcard->bundles.join(u'\n'));
        edit->setObjectName(u"WfValueEdit"_s);
        edit->setVerticalScrollBar(new AppScrollBar(Qt::Vertical));
        edit->setMinimumHeight(120);
        edit->setMaximumHeight(220);
        edit->setPlaceholderText(u"blue hair, blue eyes\nred hair\nblonde hair, twintails"_s);

        // Debounce the write; textChanged fires per keystroke.
        auto* saveTimer = new QTimer(card);
        saveTimer->setSingleShot(true);
        saveTimer->setInterval(400);
        connect(saveTimer, &QTimer::timeout, this, [this]() { save(); });

        connect(edit, &QPlainTextEdit::textChanged, this, [this, index, edit, saveTimer]() {
            QList<WorkflowVar>* vars = variables();
            if (!vars || index >= vars->size()) return;

            QStringList lines;
            for (const QString& line : edit->toPlainText().split(u'\n')) {
                const QString trimmed = line.trimmed();
                if (!trimmed.isEmpty()) lines << trimmed;
            }
            if (auto* value = std::get_if<WildcardVar>(&(*vars)[index].value))
                value->bundles = lines;
            saveTimer->start();
        });

        cardLayout->addWidget(edit);
    }

    return card;
}

void WorkflowEditPage::rebuildLoraList()
{
    clearCards(m_loraLayout);
    m_loraSubtitle->setText(u"%1 active"_s.arg(m_loraStack.size()));

    if (m_loraStack.isEmpty()) {
        auto* hint = new QLabel(u"No active LoRAs.\nActivate them from the Entry Viewer."_s);
        hint->setObjectName(u"WfEmptyHint"_s);
        hint->setAlignment(Qt::AlignCenter);
        m_loraLayout->insertWidget(0, hint);
        return;
    }

    for (int i = 0; i < int(m_loraStack.size()); ++i)
        m_loraLayout->insertWidget(i, makeLoraCard(i));
}

QFrame* WorkflowEditPage::makeLoraCard(int index)
{
    const Lora& lora = m_loraStack[index];

    // The owning entry (by sha256) stores the strengths.
    const Entry* owner = nullptr;
    if (!lora.sha256.isEmpty()) {
        for (const Entry& entry : m_entries->all()) {
            if (entry.lora && entry.lora->sha256 == lora.sha256) {
                owner = &entry;
                break;
            }
        }
    }

    auto* card = new QFrame;
    card->setObjectName(u"WfVarCard"_s);
    card->setAttribute(Qt::WA_StyledBackground, true);

    auto* row = new QHBoxLayout(card);
    row->setContentsMargins(10, 10, 12, 10);
    row->setSpacing(12);

    auto* image = new QLabel;
    image->setObjectName(u"LoraStackImage"_s);
    image->setFixedSize(80, 100);
    image->setAlignment(Qt::AlignCenter);
    image->setAttribute(Qt::WA_StyledBackground, true);

    QPixmap pixmap;
    if (owner && !owner->images.isEmpty())
        pixmap.load(m_entries->folderFor(owner->uuid) + u"/"_s + owner->images[0].fileName);
    if (pixmap.isNull()) pixmap.load(u":/img/placeholder.png"_s);
    if (!pixmap.isNull())
        image->setPixmap(pixmap.scaled(80, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    row->addWidget(image);

    auto* headerRow = new QHBoxLayout;
    headerRow->setSpacing(6);

    auto* positionBadge = new QLabel(u"#%1"_s.arg(index + 1));
    positionBadge->setObjectName(u"WfTypeLabel"_s);

    const QString display = QFileInfo(lora.file).baseName();
    auto* nameLabel = new QLabel(display.isEmpty() ? lora.file : display);
    nameLabel->setObjectName(u"WfEditSubtitle"_s);
    nameLabel->setToolTip(lora.file);

    headerRow->addWidget(positionBadge);
    headerRow->addWidget(nameLabel, 1);

    auto makeSpin = [](double min, double max, double step, double value) {
        auto* spin = new QDoubleSpinBox;
        spin->setObjectName(u"WfSpinBox"_s);
        spin->setRange(min, max);
        spin->setSingleStep(step);
        spin->setDecimals(2);
        spin->setValue(value);
        return spin;
    };

    auto* modelSpin = makeSpin(0.0, 2.0, 0.05, lora.modelStrength);
    auto* clipSpin = makeSpin(0.0, 4.0, 0.10, lora.clipStrength);

    auto* modelRow = new QHBoxLayout;
    modelRow->addWidget(fieldLabel(u"Model"_s));
    modelRow->addWidget(modelSpin, 1);

    auto* clipRow = new QHBoxLayout;
    clipRow->addWidget(fieldLabel(u"Clip"_s));
    clipRow->addWidget(clipSpin, 1);

    auto* infoColumn = new QVBoxLayout;
    infoColumn->setSpacing(6);
    infoColumn->addLayout(headerRow);
    infoColumn->addLayout(modelRow);
    infoColumn->addLayout(clipRow);

    row->addLayout(infoColumn, 1);

    // No owning entry means nowhere to save, so read-only.
    if (!owner) {
        modelSpin->setEnabled(false);
        clipSpin->setEnabled(false);
        nameLabel->setText(nameLabel->text() + u"  (entry missing)"_s);
        return card;
    }

    const QString ownerUuid = owner->uuid;
    auto onChanged = [this, index, ownerUuid, modelSpin, clipSpin]() {
        if (index < 0 || index >= m_loraStack.size()) return;

        Lora& cached = m_loraStack[index];
        cached.modelStrength = modelSpin->value();
        cached.clipStrength = clipSpin->value();

        const Entry* entry = m_entries->find(ownerUuid);
        if (entry && entry->lora) {
            Lora updated = *entry->lora;
            updated.modelStrength = cached.modelStrength;
            updated.clipStrength = cached.clipStrength;
            m_entries->setLora(ownerUuid, updated);
        }

        emit loraStrengthsChanged(m_loraStack);
    };
    connect(modelSpin, &QDoubleSpinBox::valueChanged, this, [onChanged](double) { onChanged(); });
    connect(clipSpin, &QDoubleSpinBox::valueChanged, this, [onChanged](double) { onChanged(); });

    return card;
}

} // namespace tc

#include "workflow_edit_page.moc"
