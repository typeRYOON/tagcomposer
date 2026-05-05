#include <gui/workfloweditpage.h>
#include <gui/composer/clipeditordialog.h>
#include <utils/appconfig.h>
#include <gui/widgets/appscrollbar.h>
#include <gui/widgets/composericons.h>
#include <QImage>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QPixmap>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QListWidget>
#include <QListWidgetItem>
#include <QFileDialog>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QScrollArea>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <climits>

// ── RatioPreview ─────────────────────────────────────────────────────────────
// Paints a centered rectangle scaled to the given w:h aspect ratio.

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
        p.fillRect(rect(), QColor("#0a0a0a"));
        p.setPen(QColor("#1e1e1e"));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
        if (m_w <= 0 || m_h <= 0) return;
        constexpr int pad = 8;
        const double aw = width() - 2.0 * pad;
        const double ah = height() - 2.0 * pad;
        double rw = aw, rh = ah;
        if (double(m_w) / m_h > aw / ah)
            rh = rw * double(m_h) / m_w;
        else
            rw = rh * double(m_w) / m_h;
        const QRectF r(pad + (aw - rw) / 2.0, pad + (ah - rh) / 2.0, rw, rh);
        p.fillRect(r, QColor("#1a2e1a"));
        p.setPen(QColor("#336633"));
        p.drawRect(r);
    }

private:
    int m_w = 0, m_h = 0;
};

// ── DropImageLabel ───────────────────────────────────────────────────────────
// QLabel that accepts dropped image files and emits the local path of the
// first one. Used as the thumbnail/drop target on the Image var card.

class DropImageLabel : public QLabel {
    Q_OBJECT
public:
    explicit DropImageLabel(QWidget* parent = nullptr) : QLabel(parent)
    {
        setAcceptDrops(true);
    }
signals:
    void filePathDropped(QString path);

protected:
    void dragEnterEvent(QDragEnterEvent* e) override
    {
        if (hasImageUrl(e->mimeData())) e->acceptProposedAction();
    }
    void dragMoveEvent(QDragMoveEvent* e) override
    {
        if (hasImageUrl(e->mimeData())) e->acceptProposedAction();
    }
    void dropEvent(QDropEvent* e) override
    {
        for (const QUrl& u : e->mimeData()->urls()) {
            if (!u.isLocalFile()) continue;
            const QString p = u.toLocalFile();
            if (looksLikeImage(p)) {
                emit filePathDropped(p);
                e->acceptProposedAction();
                return;
            }
        }
    }

private:
    static bool looksLikeImage(const QString& path)
    {
        const QString lower = path.toLower();
        return lower.endsWith(".png") || lower.endsWith(".jpg") || lower.endsWith(".jpeg") ||
               lower.endsWith(".bmp") || lower.endsWith(".webp") || lower.endsWith(".tif") ||
               lower.endsWith(".tiff");
    }
    static bool hasImageUrl(const QMimeData* md)
    {
        if (!md->hasUrls()) return false;
        for (const QUrl& u : md->urls())
            if (u.isLocalFile() && looksLikeImage(u.toLocalFile())) return true;
        return false;
    }
};

namespace gui {

WorkflowEditPage::WorkflowEditPage(QWidget* parent) : QWidget(parent)
{
    setObjectName("WorkflowEditPage");
    setAttribute(Qt::WA_StyledBackground, true);

    // ── Left: workflow variables ──────────────────────────────────────────────
    constexpr int kHeaderHeight = 50; // same for all section headers

    auto* leftHeader = new QWidget;
    leftHeader->setObjectName("WfEditHeader");
    leftHeader->setAttribute(Qt::WA_StyledBackground, true);
    leftHeader->setFixedHeight(kHeaderHeight);

    auto* leftHeaderLayout = new QHBoxLayout(leftHeader);
    leftHeaderLayout->setContentsMargins(20, 12, 20, 12);
    leftHeaderLayout->setSpacing(16);

    auto* sectionLabel = new QLabel("WORKFLOW VARIABLES");
    sectionLabel->setObjectName("WfEditTitle");

    m_titleLabel = new QLabel("No workflow selected");
    m_titleLabel->setObjectName("WfEditSubtitle");

    auto* openWfBtn = new QPushButton;
    openWfBtn->setObjectName("SidebarBtn");
    openWfBtn->setFixedSize(24, 24);
    openWfBtn->setIcon(gui::icons::openExternal());
    openWfBtn->setIconSize(QSize(14, 14));
    openWfBtn->setCursor(Qt::PointingHandCursor);
    openWfBtn->setToolTip("Open the selected workflow JSON in your editor");
    connect(openWfBtn, &QPushButton::clicked, this, [this]() {
        if (!m_wm) return;
        const core::WorkflowFile* wf = m_wm->selectedFile();
        if (!wf || wf->path.isEmpty()) return;
        QDesktopServices::openUrl(QUrl::fromLocalFile(wf->path));
    });

    m_addBtn = new QPushButton("+ Add Variable");
    m_addBtn->setObjectName("WfAddBtn");
    m_addBtn->setCursor(Qt::PointingHandCursor);

    leftHeaderLayout->addWidget(sectionLabel);
    leftHeaderLayout->addWidget(m_titleLabel, 1);
    leftHeaderLayout->addWidget(openWfBtn);
    leftHeaderLayout->addWidget(m_addBtn);

    m_varContainer = new QWidget;
    m_varLayout = new QVBoxLayout(m_varContainer);
    m_varLayout->setContentsMargins(20, 16, 20, 20);
    m_varLayout->setSpacing(10);
    m_varLayout->addStretch();

    auto* leftScroll = new QScrollArea;
    leftScroll->setObjectName("WfEditScroll");
    leftScroll->setWidget(m_varContainer);
    leftScroll->setWidgetResizable(true);
    leftScroll->setFrameShape(QFrame::NoFrame);
    leftScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    leftScroll->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));

    auto* leftPanel = new QWidget;
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    leftLayout->addWidget(leftHeader);
    leftLayout->addWidget(leftScroll, 1);

    // ── Middle: LoRA stack viewer/editor ──────────────────────────────────────
    auto* middleHeader = new QWidget;
    middleHeader->setObjectName("WfEditHeader");
    middleHeader->setAttribute(Qt::WA_StyledBackground, true);
    middleHeader->setFixedHeight(kHeaderHeight);

    auto* middleHeaderLayout = new QHBoxLayout(middleHeader);
    middleHeaderLayout->setContentsMargins(20, 12, 20, 12);
    middleHeaderLayout->setSpacing(16);

    auto* loraSectionLabel = new QLabel("LORA STACK");
    loraSectionLabel->setObjectName("WfEditTitle");

    m_loraSubtitle = new QLabel("0 active");
    m_loraSubtitle->setObjectName("WfEditSubtitle");

    middleHeaderLayout->addWidget(loraSectionLabel);
    middleHeaderLayout->addWidget(m_loraSubtitle, 1);

    m_loraContainer = new QWidget;
    m_loraLayout = new QVBoxLayout(m_loraContainer);
    m_loraLayout->setContentsMargins(20, 16, 20, 20);
    m_loraLayout->setSpacing(10);
    m_loraLayout->addStretch();

    auto* middleScroll = new QScrollArea;
    middleScroll->setObjectName("WfEditScroll");
    middleScroll->setWidget(m_loraContainer);
    middleScroll->setWidgetResizable(true);
    middleScroll->setFrameShape(QFrame::NoFrame);
    middleScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    middleScroll->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));

    auto* middlePanel = new QWidget;
    auto* middleLayout = new QVBoxLayout(middlePanel);
    middleLayout->setContentsMargins(0, 0, 0, 0);
    middleLayout->setSpacing(0);
    middleLayout->addWidget(middleHeader);
    middleLayout->addWidget(middleScroll, 1);

    // ── Right: Batch ──────────────────────────────────────────────────────────
    // Fire-and-forget: query, resolve entries, build a prompt per entry
    // (composer state union entry tags), push N prompts to ComfyUI.
    // Non-blocking; ComfyUI processes the queue serially on its end.
    auto* rightHeader = new QWidget;
    rightHeader->setObjectName("WfEditHeader");
    rightHeader->setAttribute(Qt::WA_StyledBackground, true);
    rightHeader->setFixedHeight(kHeaderHeight);

    auto* rightHeaderLayout = new QHBoxLayout(rightHeader);
    rightHeaderLayout->setContentsMargins(20, 12, 20, 12);
    rightHeaderLayout->setSpacing(16);

    auto* batchTitle = new QLabel("BATCH");
    batchTitle->setObjectName("WfEditTitle");
    rightHeaderLayout->addWidget(batchTitle);
    rightHeaderLayout->addStretch();

    auto* batchBody = new QWidget;
    auto* batchBodyLayout = new QVBoxLayout(batchBody);
    batchBodyLayout->setContentsMargins(20, 16, 20, 20);
    batchBodyLayout->setSpacing(8);

    auto* batchHint = new QLabel("Run the current composer prompt across every entry matching the "
                                 "query - same syntax as the tile-view search bar.");
    batchHint->setObjectName("WfEmptyHint");
    batchHint->setWordWrap(true);
    batchBodyLayout->addWidget(batchHint);

    auto* batchQueryEdit = new QLineEdit;
    batchQueryEdit->setObjectName("WfValueEdit");
    batchQueryEdit->setPlaceholderText("entry query, e.g. \"kantai collection, -nsfw\"");
    batchBodyLayout->addWidget(batchQueryEdit);

    m_batchCountLabel = new QLabel("0 entries");
    m_batchCountLabel->setObjectName("WfEditSubtitle");
    batchBodyLayout->addWidget(m_batchCountLabel);

    m_batchResultsList = new QListWidget;
    m_batchResultsList->setObjectName("WfFileList");
    m_batchResultsList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_batchResultsList->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));
    m_batchResultsList->setSelectionMode(QAbstractItemView::NoSelection);
    m_batchResultsList->setFocusPolicy(Qt::NoFocus);
    batchBodyLayout->addWidget(m_batchResultsList, 1);

    auto* batchRunBtn = new QPushButton(" Run Batch");
    batchRunBtn->setObjectName("WfAddBtn");
    // #66aa66 is the hover-shade green from the WfAddBtn theme - matches
    // the button's text color so the icon and label read as one piece.
    batchRunBtn->setIcon(gui::icons::play(14, QColor(0x66, 0xaa, 0x66)));
    batchRunBtn->setIconSize(QSize(12, 12));
    batchRunBtn->setCursor(Qt::PointingHandCursor);

    m_batchStatus = new QLabel;
    m_batchStatus->setObjectName("WfEditSubtitle");
    m_batchStatus->setWordWrap(true);

    auto* batchBtnRow = new QHBoxLayout;
    batchBtnRow->setSpacing(10);
    batchBtnRow->addWidget(batchRunBtn);
    batchBtnRow->addWidget(m_batchStatus, 1);
    batchBodyLayout->addLayout(batchBtnRow);

    connect(batchRunBtn, &QPushButton::clicked, this,
            [this, batchQueryEdit]() { emit batchRunRequested(batchQueryEdit->text().trimmed()); });
    // Enter in the query field triggers the run too.
    connect(batchQueryEdit, &QLineEdit::returnPressed, this,
            [this, batchQueryEdit]() { emit batchRunRequested(batchQueryEdit->text().trimmed()); });

    // Live results preview: debounce keystrokes, then resolve via EntryModel.
    m_batchQueryDebounce = new QTimer(this);
    m_batchQueryDebounce->setSingleShot(true);
    m_batchQueryDebounce->setInterval(120);
    connect(batchQueryEdit, &QLineEdit::textChanged, m_batchQueryDebounce,
            qOverload<>(&QTimer::start));
    connect(m_batchQueryDebounce, &QTimer::timeout, this, [this, batchQueryEdit]() {
        m_batchResultsList->clear();
        if (!m_entryModel) {
            m_batchCountLabel->setText("entries unavailable");
            return;
        }
        const auto matched = m_entryModel->filter(batchQueryEdit->text().trimmed());
        m_batchCountLabel->setText(QString("%1 entr%2 matched")
                                       .arg(matched.size())
                                       .arg(matched.size() == 1 ? "y" : "ies"));
        for (const auto* e : matched) {
            const QString display = e->title.isEmpty() ? e->uuid : e->title;
            m_batchResultsList->addItem(display);
        }
    });

    auto* rightPanel = new QWidget;
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    rightLayout->addWidget(rightHeader);
    rightLayout->addWidget(batchBody, 1); // body fills the column

    // ── Root: three equal columns spanning the full page width ───────────────
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(leftPanel, 1);
    root->addWidget(middlePanel, 1);
    root->addWidget(rightPanel, 1);

    // ── Add variable menu ─────────────────────────────────────────────────────
    connect(m_addBtn, &QPushButton::clicked, this, [this]() {
        QMenu menu(this);
        menu.addAction("Seed", this, [this]() { addVariable(core::WorkflowVarType::Seed); });
        menu.addAction("String", this, [this]() { addVariable(core::WorkflowVarType::String); });
        menu.addAction("Integer", this, [this]() { addVariable(core::WorkflowVarType::Integer); });
        menu.addAction("Float", this, [this]() { addVariable(core::WorkflowVarType::Float); });
        menu.addAction("Dir Search", this,
                       [this]() { addVariable(core::WorkflowVarType::DirSearch); });
        menu.addAction("Latent Size", this,
                       [this]() { addVariable(core::WorkflowVarType::LatentSize); });
        menu.addAction("Image", this, [this]() { addVariable(core::WorkflowVarType::Image); });
        menu.addAction("Wildcard", this,
                       [this]() { addVariable(core::WorkflowVarType::Wildcard); });
        menu.exec(m_addBtn->mapToGlobal(QPoint(0, m_addBtn->height())));
    });

    rebuildVarList();
    rebuildLoraList();
}

void WorkflowEditPage::setWorkflowManager(core::WorkflowManager* wm, const QString& savePath)
{
    m_wm = wm;
    m_savePath = savePath;
    refresh();
}

void WorkflowEditPage::setInputCache(core::WorkflowInputCache* cache)
{
    m_inputCache = cache;
    rebuildVarList();
}

void WorkflowEditPage::setEntryModel(core::EntryModel* model)
{
    m_entryModel = model;
    rebuildLoraList();
    // Populate the batch preview now that we can resolve queries.
    if (m_batchQueryDebounce) m_batchQueryDebounce->start();
}

void WorkflowEditPage::setBatchResult(const QString& message)
{
    if (m_batchStatus) m_batchStatus->setText(message);
}

void WorkflowEditPage::setActiveLoraStack(const QList<core::LoraConfig>& stack)
{
    m_loraStack = stack;
    rebuildLoraList();
}

void WorkflowEditPage::refresh()
{
    if (m_wm) {
        const core::WorkflowFile* wf = m_wm->selectedFile();
        m_titleLabel->setText(wf ? wf->name : "No workflow selected");
    }
    else {
        m_titleLabel->setText("No workflow selected");
    }
    rebuildVarList();
}

void WorkflowEditPage::save()
{
    if (m_wm && !m_savePath.isEmpty()) m_wm->saveToFile(m_savePath);
}

void WorkflowEditPage::addVariable(core::WorkflowVarType type)
{
    if (!m_wm) return;
    core::WorkflowVar var;
    var.type = type;
    // Wildcards don't substitute a __TOKEN__ in the workflow JSON - they
    // inject into the positive prompt - so they don't need a placeholder.
    if (type != core::WorkflowVarType::Wildcard) var.placeholder = "__NEW__";
    m_wm->variables() << var;
    save();
    rebuildVarList();
}

void WorkflowEditPage::removeVariable(int index)
{
    if (!m_wm || index < 0 || index >= m_wm->variables().size()) return;
    m_wm->variables().removeAt(index);
    save();
    rebuildVarList();
}

void WorkflowEditPage::rebuildVarList()
{
    // Remove all items except the trailing stretch (always at last position)
    while (m_varLayout->count() > 1) {
        QLayoutItem* item = m_varLayout->takeAt(0);
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }

    if (!m_wm || m_wm->variables().isEmpty()) {
        auto* hint = new QLabel("No variables defined.\nClick \"+ Add Variable\" to create one.");
        hint->setObjectName("WfEmptyHint");
        hint->setAlignment(Qt::AlignCenter);
        m_varLayout->insertWidget(0, hint);
        return;
    }

    for (int i = 0; i < m_wm->variables().size(); ++i)
        m_varLayout->insertWidget(i, makeVarCard(i));
}

QFrame* WorkflowEditPage::makeVarCard(int index)
{
    auto& var = m_wm->variables()[index];

    auto* card = new QFrame;
    card->setObjectName("WfVarCard");
    card->setAttribute(Qt::WA_StyledBackground, true);
    // Cap height to sizeHint so cards don't stretch vertically when the
    // m_varLayout has extra space - extra space goes to the trailing stretch.
    card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(12, 10, 12, 12);
    cardLayout->setSpacing(8);

    // ── Header row: placeholder name | type badge | remove ───────────────────
    auto* headerRow = new QHBoxLayout;
    headerRow->setSpacing(6);

    auto* placeholderEdit = new QLineEdit(var.placeholder);
    placeholderEdit->setObjectName("WfPlaceholderEdit");
    // Wildcards inject into the positive prompt, not into __TOKEN__ slots in
    // the workflow JSON, so this field is just a label for organization.
    placeholderEdit->setPlaceholderText(
        var.type == core::WorkflowVarType::Wildcard ? "Name (label only)" : "__PLACEHOLDER__");
    connect(placeholderEdit, &QLineEdit::editingFinished, this, [this, index, placeholderEdit]() {
        if (!m_wm || index >= m_wm->variables().size()) return;
        m_wm->variables()[index].placeholder = placeholderEdit->text().trimmed();
        save();
    });

    static const char* kTypeNames[] = {"Seed",       "String",      "Integer", "Float",
                                       "Dir Search", "Latent Size", "Image",   "Wildcard"};
    auto* typeLabel = new QLabel(kTypeNames[int(var.type)]);
    typeLabel->setObjectName("WfTypeLabel");

    auto* removeBtn = new QPushButton("×");
    removeBtn->setObjectName("WfRemoveBtn");
    removeBtn->setFixedSize(20, 20);
    removeBtn->setCursor(Qt::PointingHandCursor);
    connect(removeBtn, &QPushButton::clicked, this, [this, index]() { removeVariable(index); });

    headerRow->addWidget(placeholderEdit, 1);
    headerRow->addWidget(typeLabel);
    headerRow->addWidget(removeBtn);
    cardLayout->addLayout(headerRow);

    // ── Type-specific body ────────────────────────────────────────────────────
    switch (var.type) {
    case core::WorkflowVarType::Seed: {
        // Behavior radio buttons
        auto* behRow = new QHBoxLayout;
        behRow->setSpacing(10);

        auto* bg = new QButtonGroup(card);
        auto addRadio = [&](const QString& text, core::SeedBehavior b) {
            auto* rb = new QRadioButton(text);
            rb->setObjectName("WfRadio");
            rb->setChecked(var.seedBehavior == b);
            bg->addButton(rb, int(b));
            behRow->addWidget(rb);
        };
        addRadio("Fixed", core::SeedBehavior::Fixed);
        addRadio("Increment", core::SeedBehavior::Increment);
        addRadio("Randomize", core::SeedBehavior::Randomize);
        behRow->addStretch();
        cardLayout->addLayout(behRow);

        connect(bg, &QButtonGroup::idClicked, this, [this, index](int id) {
            if (!m_wm || index >= m_wm->variables().size()) return;
            m_wm->variables()[index].seedBehavior = core::SeedBehavior(id);
            save();
        });

        // Seed value
        auto* valRow = new QHBoxLayout;
        auto* valLabel = new QLabel("Value:");
        valLabel->setObjectName("WfFieldLabel");

        auto* seedEdit = new QLineEdit;
        seedEdit->setObjectName("WfValueEdit"); // dark themed; matches other value fields
        seedEdit->setText(QString::number(var.seedValue));

        connect(seedEdit, &QLineEdit::editingFinished, this, [this, index, seedEdit]() {
            bool ok = false;
            qint64 v = seedEdit->text().toLongLong(&ok);
            if (!ok) return;

            if (!m_wm || index >= m_wm->variables().size()) return;
            m_wm->variables()[index].seedValue = v;
            save();
        });

        valRow->addWidget(valLabel);
        valRow->addWidget(seedEdit, 1);
        cardLayout->addLayout(valRow);
        break;
    }

    case core::WorkflowVarType::String: {
        auto* valueEdit = new QLineEdit(var.stringValue);
        valueEdit->setObjectName("WfValueEdit");
        valueEdit->setPlaceholderText("Replacement text");
        connect(valueEdit, &QLineEdit::editingFinished, this, [this, index, valueEdit]() {
            if (!m_wm || index >= m_wm->variables().size()) return;
            m_wm->variables()[index].stringValue = valueEdit->text();
            save();
        });
        cardLayout->addWidget(valueEdit);
        break;
    }

    case core::WorkflowVarType::Integer: {
        auto* valRow = new QHBoxLayout;
        auto* valLabel = new QLabel("Value:");
        valLabel->setObjectName("WfFieldLabel");

        auto* intSpin = new QSpinBox;
        intSpin->setObjectName("WfSpinBox");
        intSpin->setRange(INT_MIN, INT_MAX);
        intSpin->setValue(var.intValue);
        connect(intSpin, &QSpinBox::valueChanged, this, [this, index](int v) {
            if (!m_wm || index >= m_wm->variables().size()) return;
            m_wm->variables()[index].intValue = v;
            save();
        });

        valRow->addWidget(valLabel);
        valRow->addWidget(intSpin, 1);
        cardLayout->addLayout(valRow);
        break;
    }

    case core::WorkflowVarType::Float: {
        auto* valRow = new QHBoxLayout;
        auto* valLabel = new QLabel("Value:");
        valLabel->setObjectName("WfFieldLabel");

        auto* floatSpin = new QDoubleSpinBox;
        floatSpin->setObjectName("WfSpinBox");
        floatSpin->setDecimals(4);
        floatSpin->setRange(-1e9, 1e9);
        floatSpin->setValue(var.floatValue);
        floatSpin->setSingleStep(0.1);
        connect(floatSpin, &QDoubleSpinBox::valueChanged, this, [this, index](double v) {
            if (!m_wm || index >= m_wm->variables().size()) return;
            m_wm->variables()[index].floatValue = v;
            save();
        });

        valRow->addWidget(valLabel);
        valRow->addWidget(floatSpin, 1);
        cardLayout->addLayout(valRow);
        break;
    }

    case core::WorkflowVarType::DirSearch: {
        // Directory path row
        auto* dirRow = new QHBoxLayout;
        dirRow->setSpacing(6);

        auto* dirEdit = new QLineEdit(var.searchDir);
        dirEdit->setObjectName("WfValueEdit");
        dirEdit->setPlaceholderText("Search directory...");

        auto* browseBtn = new QPushButton("Browse");
        browseBtn->setObjectName("WfBrowseBtn");
        browseBtn->setCursor(Qt::PointingHandCursor);

        dirRow->addWidget(dirEdit, 1);
        dirRow->addWidget(browseBtn);
        cardLayout->addLayout(dirRow);

        // Extension whitelist (persisted); empty = block .sha256 only
        auto* extEdit = new QLineEdit(var.extensionFilter);
        extEdit->setObjectName("WfValueEdit");
        extEdit->setPlaceholderText(
            "Ext. whitelist: .safetensors, .ckpt  (empty = all except .sha256)");
        cardLayout->addWidget(extEdit);

        // Name filter (transient, not persisted)
        auto* filterEdit = new QLineEdit;
        filterEdit->setObjectName("WfValueEdit");
        filterEdit->setPlaceholderText("Filter by name...");
        cardLayout->addWidget(filterEdit);

        // File list
        auto* fileList = new QListWidget;
        fileList->setObjectName("WfFileList");
        fileList->setFixedHeight(200);
        fileList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        fileList->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));
        cardLayout->addWidget(fileList);

        // Shared scan function
        auto scanDir = [this, index, dirEdit, extEdit, filterEdit, fileList]() {
            fileList->clear();
            const QString dir = dirEdit->text().trimmed();
            const QString filter = filterEdit->text().trimmed();
            if (dir.isEmpty()) return;

            // Guard: never recurse the filesystem root
            if (QDir(dir).isRoot()) return;

            // Build extension set from whitelist field
            const QString extRaw = extEdit->text().trimmed();
            QSet<QString> extWhitelist;
            if (!extRaw.isEmpty()) {
                for (const QString& e : extRaw.split(',')) {
                    const QString ext = e.trimmed().toLower();
                    if (!ext.isEmpty()) extWhitelist.insert(ext.startsWith('.') ? ext : '.' + ext);
                }
            }

            const QDir base(dir);
            const QString curSel = (m_wm && index < m_wm->variables().size())
                                       ? m_wm->variables()[index].selectedFile
                                       : QString();
            QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
            int n = 0;
            while (it.hasNext() && n < 500) {
                it.next();
                const QString ext = QFileInfo(it.fileName()).suffix().toLower().prepend('.');

                if (extWhitelist.isEmpty()) {
                    if (ext == ".sha256") continue; // default blacklist
                }
                else {
                    if (!extWhitelist.contains(ext)) continue;
                }

                if (!filter.isEmpty() && !it.fileName().contains(filter, Qt::CaseInsensitive))
                    continue;

                const QString relPath = base.relativeFilePath(it.filePath());
                auto* item = new QListWidgetItem(relPath);
                item->setData(Qt::UserRole, it.filePath());
                item->setToolTip(it.filePath());
                if (it.filePath() == curSel) {
                    QFont f = item->font();
                    f.setBold(true);
                    item->setFont(f);
                    item->setForeground(QColor("#5a9a5a"));
                }
                fileList->addItem(item);
                ++n;
            }
        };

        connect(browseBtn, &QPushButton::clicked, this, [this, index, dirEdit, scanDir]() {
            const QString dir =
                QFileDialog::getExistingDirectory(this, "Select Directory", dirEdit->text());
            if (dir.isEmpty()) return;
            dirEdit->setText(dir);
            if (m_wm && index < m_wm->variables().size()) {
                m_wm->variables()[index].searchDir = dir;
                save();
            }
            scanDir();
        });

        connect(dirEdit, &QLineEdit::editingFinished, this, [this, index, dirEdit, scanDir]() {
            if (m_wm && index < m_wm->variables().size()) {
                m_wm->variables()[index].searchDir = dirEdit->text().trimmed();
                save();
            }
            scanDir();
        });

        connect(extEdit, &QLineEdit::editingFinished, this, [this, index, extEdit, scanDir]() {
            if (m_wm && index < m_wm->variables().size()) {
                m_wm->variables()[index].extensionFilter = extEdit->text().trimmed();
                save();
            }
            scanDir();
        });

        connect(filterEdit, &QLineEdit::textChanged, this,
                [scanDir](const QString&) { scanDir(); });

        connect(fileList, &QListWidget::itemClicked, this,
                [this, index, fileList](QListWidgetItem* item) {
                    if (!m_wm || index >= m_wm->variables().size()) return;
                    const QString path = item->data(Qt::UserRole).toString();
                    m_wm->variables()[index].selectedFile = path;
                    save();
                    for (int i = 0; i < fileList->count(); ++i) {
                        QListWidgetItem* it = fileList->item(i);
                        const bool sel = it->data(Qt::UserRole).toString() == path;
                        QFont f = it->font();
                        f.setBold(sel);
                        it->setFont(f);
                        it->setForeground(QColor(sel ? "#5a9a5a" : "#666666"));
                    }
                });

        if (!var.searchDir.isEmpty()) scanDir();
        break;
    }

    case core::WorkflowVarType::LatentSize: {
        const QString sizesPath = utils::BASE_PATH + "/" + utils::LATENT_SIZES_PATH;
        const QList<core::LatentSizeEntry> sizes =
            core::WorkflowManager::loadLatentSizes(sizesPath);

        auto* bodyRow = new QHBoxLayout;
        bodyRow->setSpacing(8);
        bodyRow->setAlignment(Qt::AlignTop);

        auto* sizeList = new QListWidget;
        sizeList->setObjectName("WfFileList");
        sizeList->setFixedHeight(150);
        sizeList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        sizeList->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));

        auto* ratioPreview = new RatioPreview;

        if (sizes.isEmpty()) {
            auto* hint =
                new QListWidgetItem(QString("File not found: %1").arg(utils::LATENT_SIZES_PATH));
            hint->setFlags(Qt::NoItemFlags);
            QFont f = hint->font();
            f.setItalic(true);
            hint->setFont(f);
            sizeList->addItem(hint);
        }
        else {
            for (const core::LatentSizeEntry& e : sizes) {
                auto* item = new QListWidgetItem(e.label);
                item->setData(Qt::UserRole, e.w);
                item->setData(Qt::UserRole + 1, e.h);
                item->setData(Qt::UserRole + 2, e.label);
                const bool sel = (e.label == var.stringValue);
                if (sel) {
                    QFont f = item->font();
                    f.setBold(true);
                    item->setFont(f);
                    item->setForeground(QColor("#5a9a5a"));
                    ratioPreview->setRatio(e.w, e.h);
                }
                sizeList->addItem(item);
            }
        }

        connect(sizeList, &QListWidget::itemClicked, this,
                [this, index, sizeList, ratioPreview](QListWidgetItem* item) {
                    if (!m_wm || index >= m_wm->variables().size()) return;
                    const QString label = item->data(Qt::UserRole + 2).toString();
                    if (label.isEmpty()) return;
                    const int w = item->data(Qt::UserRole).toInt();
                    const int h = item->data(Qt::UserRole + 1).toInt();
                    m_wm->variables()[index].stringValue = label;
                    save();
                    ratioPreview->setRatio(w, h);
                    for (int i = 0; i < sizeList->count(); ++i) {
                        auto* it = sizeList->item(i);
                        const bool sel = it->data(Qt::UserRole + 2).toString() == label;
                        QFont f = it->font();
                        f.setBold(sel);
                        it->setFont(f);
                        it->setForeground(QColor(sel ? "#5a9a5a" : "#666666"));
                    }
                });

        bodyRow->addWidget(sizeList, 1);
        bodyRow->addWidget(ratioPreview, 0, Qt::AlignTop);
        cardLayout->addLayout(bodyRow);
        break;
    }

    case core::WorkflowVarType::Image: {
        auto* bodyRow = new QHBoxLayout;
        bodyRow->setSpacing(8);
        bodyRow->setAlignment(Qt::AlignTop);

        auto* thumb = new DropImageLabel;
        thumb->setObjectName("WfImageThumb");
        thumb->setFixedSize(96, 96);
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setFrameShape(QFrame::StyledPanel);
        thumb->setToolTip("Drop an image file here, or use Browse…");

        auto* nameLabel = new QLabel;
        nameLabel->setObjectName("WfFieldLabel");
        nameLabel->setWordWrap(true);

        auto refresh = [thumb, nameLabel, this, index](const QString& uuid) {
            if (!uuid.isEmpty() && m_inputCache && m_inputCache->has(uuid)) {
                QPixmap pm(m_inputCache->localPath(uuid));
                if (!pm.isNull()) {
                    thumb->setPixmap(
                        pm.scaled(thumb->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                }
                else {
                    thumb->setText("(broken)");
                }
                const auto rec = m_inputCache->get(uuid);
                QString name = rec.displayName.isEmpty() ? uuid : rec.displayName;
                // Surface active edits in the label so the user can tell at a
                // glance that the upload won't be the raw source.
                if (m_wm && index < m_wm->variables().size() &&
                    m_wm->variables()[index].imageEdits.enabled) {
                    const auto& e = m_wm->variables()[index].imageEdits;
                    const QRect r = e.cropRect;
                    name +=
                        e.trimToCrop
                            ? QStringLiteral("  •  cropped %1×%2").arg(r.width()).arg(r.height())
                            : QStringLiteral("  •  mask %1×%2").arg(r.width()).arg(r.height());
                }
                nameLabel->setText(name);
            }
            else {
                thumb->clear();
                thumb->setText("(no image)");
                nameLabel->setText(uuid.isEmpty() ? QStringLiteral("No image selected")
                                                  : QStringLiteral("Missing: %1").arg(uuid));
            }
        };
        refresh(var.imageUuid);

        auto* browseBtn = new QPushButton("Browse…");
        browseBtn->setObjectName("WfBrowseBtn");
        browseBtn->setCursor(Qt::PointingHandCursor);

        auto* editBtn = new QPushButton("Edit…");
        editBtn->setObjectName("WfBrowseBtn");
        editBtn->setCursor(Qt::PointingHandCursor);
        editBtn->setEnabled(!var.imageUuid.isEmpty() && m_inputCache &&
                            m_inputCache->has(var.imageUuid));

        auto* clearBtn = new QPushButton("Clear");
        clearBtn->setObjectName("WfBrowseBtn");
        clearBtn->setCursor(Qt::PointingHandCursor);

        // Re-evaluate edit-button state after any image change. Captured by
        // both Browse and drop handlers below.
        auto updateEditEnabled = [editBtn, this, index]() {
            if (!m_wm || index >= m_wm->variables().size()) {
                editBtn->setEnabled(false);
                return;
            }
            const QString uuid = m_wm->variables()[index].imageUuid;
            editBtn->setEnabled(!uuid.isEmpty() && m_inputCache && m_inputCache->has(uuid));
        };

        // Image source changed: drop edits and the old mask so it doesn't
        // accumulate on disk.
        auto resetEdits = [this, index]() {
            if (!m_wm || index >= m_wm->variables().size()) return;
            const QString oldMaskId = m_wm->variables()[index].imageEdits.maskId;
            m_wm->variables()[index].imageEdits = core::ImageEdits{};
            if (m_inputCache && !oldMaskId.isEmpty()) m_inputCache->removeMask(oldMaskId);
        };

        connect(browseBtn, &QPushButton::clicked, this,
                [this, index, refresh, updateEditEnabled, resetEdits]() {
                    if (!m_wm || !m_inputCache || index >= m_wm->variables().size()) return;
                    const QString src = QFileDialog::getOpenFileName(
                        this, "Select image", QString(),
                        "Images (*.png *.jpg *.jpeg *.bmp *.webp *.tiff)");
                    if (src.isEmpty()) return;
                    const QString uuid = m_inputCache->importFromFile(src);
                    if (uuid.isEmpty()) return;
                    resetEdits();
                    m_wm->variables()[index].imageUuid = uuid;
                    save();
                    refresh(uuid);
                    updateEditEnabled();
                });

        connect(clearBtn, &QPushButton::clicked, this,
                [this, index, refresh, updateEditEnabled, resetEdits]() {
                    if (!m_wm || index >= m_wm->variables().size()) return;
                    resetEdits();
                    m_wm->variables()[index].imageUuid.clear();
                    save();
                    refresh(QString());
                    updateEditEnabled();
                });

        connect(thumb, &DropImageLabel::filePathDropped, this,
                [this, index, refresh, updateEditEnabled, resetEdits](const QString& src) {
                    if (!m_wm || !m_inputCache || index >= m_wm->variables().size()) return;
                    const QString uuid = m_inputCache->importFromFile(src);
                    if (uuid.isEmpty()) return;
                    resetEdits();
                    m_wm->variables()[index].imageUuid = uuid;
                    save();
                    refresh(uuid);
                    updateEditEnabled();
                });

        connect(editBtn, &QPushButton::clicked, this, [this, index, refresh]() {
            if (!m_wm || !m_inputCache || index >= m_wm->variables().size()) return;
            const QString uuid = m_wm->variables()[index].imageUuid;
            if (uuid.isEmpty() || !m_inputCache->has(uuid)) return;

            QImage src(m_inputCache->localPath(uuid));
            if (src.isNull()) return;

            const QString oldMaskId = m_wm->variables()[index].imageEdits.maskId;

            gui::ClipEditorDialog dlg(src, m_wm->variables()[index].imageEdits, m_inputCache, this);
            if (dlg.exec() != QDialog::Accepted) return;
            m_wm->variables()[index].imageEdits = dlg.result();

            // saveMask always mints a fresh uuid, so any change leaves
            // the old file orphaned - drop it.
            const QString newMaskId = m_wm->variables()[index].imageEdits.maskId;
            if (!oldMaskId.isEmpty() && oldMaskId != newMaskId) m_inputCache->removeMask(oldMaskId);

            save();
            refresh(uuid);
        });

        auto* btnCol = new QVBoxLayout;
        btnCol->setSpacing(4);
        btnCol->addWidget(nameLabel);
        auto* btnRow = new QHBoxLayout;
        btnRow->setSpacing(4);
        btnRow->addWidget(browseBtn);
        btnRow->addWidget(editBtn);
        btnRow->addWidget(clearBtn);
        btnRow->addStretch();
        btnCol->addLayout(btnRow);
        btnCol->addStretch();

        bodyRow->addWidget(thumb, 0, Qt::AlignTop);
        bodyRow->addLayout(btnCol, 1);
        cardLayout->addLayout(bodyRow);
        break;
    }

    case core::WorkflowVarType::Wildcard: {
        auto* hint = new QLabel("One slot per line. A random line is picked for each prompt run\n"
                                "and merged into the positive prompt - rules and replacement\n"
                                "vars apply. Commas split a line into multiple tags.");
        hint->setObjectName("WfFieldLabel");
        hint->setWordWrap(true);
        cardLayout->addWidget(hint);

        auto* edit = new QPlainTextEdit(var.wildcardTags.join('\n'));
        edit->setObjectName("WfValueEdit");
        edit->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));
        edit->setMinimumHeight(120);
        edit->setMaximumHeight(220);
        edit->setPlaceholderText("blue hair, blue eyes\nred hair\nblonde hair, twintails");

        // Debounce disk writes - textChanged fires on every keystroke.
        auto* saveTimer = new QTimer(card);
        saveTimer->setSingleShot(true);
        saveTimer->setInterval(400);
        connect(saveTimer, &QTimer::timeout, this, [this]() { save(); });

        connect(edit, &QPlainTextEdit::textChanged, this, [this, index, edit, saveTimer]() {
            if (!m_wm || index >= m_wm->variables().size()) return;
            QStringList lines;
            for (const QString& line : edit->toPlainText().split('\n')) {
                const QString t = line.trimmed();
                if (!t.isEmpty()) lines << t;
            }
            m_wm->variables()[index].wildcardTags = lines;
            saveTimer->start();
        });

        cardLayout->addWidget(edit);
        break;
    }

    } // switch

    return card;
}

// ── LoRA stack panel ─────────────────────────────────────────────────────────

void WorkflowEditPage::rebuildLoraList()
{
    // Drop everything except the trailing stretch
    while (m_loraLayout->count() > 1) {
        QLayoutItem* item = m_loraLayout->takeAt(0);
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }

    m_loraSubtitle->setText(QString("%1 active").arg(m_loraStack.size()));

    if (m_loraStack.isEmpty()) {
        auto* hint = new QLabel("No active LoRAs.\nActivate them from the Tile View.");
        hint->setObjectName("WfEmptyHint");
        hint->setAlignment(Qt::AlignCenter);
        m_loraLayout->insertWidget(0, hint);
        return;
    }

    for (int i = 0; i < m_loraStack.size(); ++i)
        m_loraLayout->insertWidget(i, makeLoraCard(i));
}

QFrame* WorkflowEditPage::makeLoraCard(int index)
{
    const core::LoraConfig& lc = m_loraStack[index];
    core::Entry* entry = (m_entryModel && !lc.sha256.isEmpty())
                             ? m_entryModel->entryByLoraSha256(lc.sha256)
                             : nullptr;

    auto* card = new QFrame;
    card->setObjectName("WfVarCard");
    card->setAttribute(Qt::WA_StyledBackground, true);

    auto* row = new QHBoxLayout(card);
    row->setContentsMargins(10, 10, 12, 10);
    row->setSpacing(12);

    // Left: image preview (first image of the entry, or placeholder)
    auto* imgLabel = new QLabel;
    imgLabel->setFixedSize(80, 100);
    imgLabel->setAlignment(Qt::AlignCenter);
    imgLabel->setAttribute(Qt::WA_StyledBackground, true);
    imgLabel->setObjectName("LoraStackImage");

    QPixmap pix;
    if (entry && !entry->images.isEmpty()) {
        const QString path =
            utils::BASE_PATH + "/data/entry/" + entry->uuid + "/" + entry->images[0].fileName;
        pix.load(path);
    }
    if (pix.isNull()) pix.load(":/img/placeholder.png");
    if (!pix.isNull())
        imgLabel->setPixmap(pix.scaled(80, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    row->addWidget(imgLabel);

    // Right: position badge + filename + strength spinboxes
    auto* infoCol = new QVBoxLayout;
    infoCol->setSpacing(6);

    auto* headerRow = new QHBoxLayout;
    headerRow->setSpacing(6);
    auto* posBadge = new QLabel(QString("#%1").arg(index + 1));
    posBadge->setObjectName("WfTypeLabel");

    const QString display = QFileInfo(lc.file).baseName();
    auto* nameLabel = new QLabel(display.isEmpty() ? lc.file : display);
    nameLabel->setObjectName("WfEditSubtitle");
    nameLabel->setToolTip(lc.file);

    headerRow->addWidget(posBadge);
    headerRow->addWidget(nameLabel, 1);
    infoCol->addLayout(headerRow);

    auto makeSpinBox = [](double min, double max, double step, double val) {
        auto* sb = new QDoubleSpinBox;
        sb->setObjectName("WfSpinBox");
        sb->setRange(min, max);
        sb->setSingleStep(step);
        sb->setValue(val);
        sb->setDecimals(2);
        return sb;
    };

    auto* modelSpin = makeSpinBox(0.0, 2.0, 0.05, lc.modelStr);
    auto* clipSpin = makeSpinBox(0.0, 4.0, 0.10, lc.clipStr);

    auto* modelRow = new QHBoxLayout;
    auto* modelLabel = new QLabel("Model");
    modelLabel->setObjectName("WfFieldLabel");
    modelRow->addWidget(modelLabel);
    modelRow->addWidget(modelSpin, 1);
    infoCol->addLayout(modelRow);

    auto* clipRow = new QHBoxLayout;
    auto* clipLabel = new QLabel("Clip");
    clipLabel->setObjectName("WfFieldLabel");
    clipRow->addWidget(clipLabel);
    clipRow->addWidget(clipSpin, 1);
    infoCol->addLayout(clipRow);

    row->addLayout(infoCol, 1);

    // If we couldn't find the entry, the spinboxes are display-only -
    // editing won't be persisted because there's nothing to save.
    if (!entry) {
        modelSpin->setEnabled(false);
        clipSpin->setEnabled(false);
        nameLabel->setText(nameLabel->text() + "  (entry missing)");
        return card;
    }

    auto onChanged = [this, index, modelSpin, clipSpin]() {
        if (index < 0 || index >= m_loraStack.size()) return;
        core::LoraConfig& cached = m_loraStack[index];
        cached.modelStr = modelSpin->value();
        cached.clipStr = clipSpin->value();

        if (m_entryModel) {
            core::Entry* e = m_entryModel->entryByLoraSha256(cached.sha256);
            if (e && e->lora.has_value()) {
                e->lora->modelStr = cached.modelStr;
                e->lora->clipStr = cached.clipStr;
                m_entryModel->saveEntry(e->id);
            }
        }

        emit loraStrengthsChanged(m_loraStack);
    };
    connect(modelSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [onChanged](double) { onChanged(); });
    connect(clipSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [onChanged](double) { onChanged(); });

    return card;
}

} // namespace gui

#include "workfloweditpage.moc"
