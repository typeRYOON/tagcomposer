#include <gui/workfloweditpage.h>
#include <utils/appconfig.h>
#include <gui/appscrollbar.h>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLineEdit>
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
#include <QSet>
#include <climits>

// ── RatioPreview ─────────────────────────────────────────────────────────────
// Paints a centered rectangle scaled to the given w:h aspect ratio.

class RatioPreview : public QWidget {
public:
    explicit RatioPreview(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(80, 80);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    void setRatio(int w, int h) {
        m_w = w; m_h = h;
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor("#0a0a0a"));
        p.setPen(QColor("#1e1e1e"));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
        if (m_w <= 0 || m_h <= 0) return;
        constexpr int pad = 8;
        const double aw = width()  - 2.0 * pad;
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

namespace gui {

WorkflowEditPage::WorkflowEditPage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("WorkflowEditPage");
    setAttribute(Qt::WA_StyledBackground, true);

    // ── Header bar ────────────────────────────────────────────────────────────
    auto* headerBar = new QWidget;
    headerBar->setObjectName("WfEditHeader");
    headerBar->setAttribute(Qt::WA_StyledBackground, true);

    auto* headerLayout = new QHBoxLayout(headerBar);
    headerLayout->setContentsMargins(20, 12, 20, 12);
    headerLayout->setSpacing(16);

    auto* sectionLabel = new QLabel("WORKFLOW VARIABLES");
    sectionLabel->setObjectName("WfEditTitle");

    m_titleLabel = new QLabel("No workflow selected");
    m_titleLabel->setObjectName("WfEditSubtitle");

    auto* addBtn = new QPushButton("+ Add Variable");
    addBtn->setObjectName("WfAddBtn");
    addBtn->setCursor(Qt::PointingHandCursor);

    headerLayout->addWidget(sectionLabel);
    headerLayout->addWidget(m_titleLabel, 1);
    headerLayout->addWidget(addBtn);

    // ── Scroll area ───────────────────────────────────────────────────────────
    m_varContainer = new QWidget;
    m_varLayout    = new QVBoxLayout(m_varContainer);
    m_varLayout->setContentsMargins(20, 16, 20, 20);
    m_varLayout->setSpacing(10);
    m_varLayout->addStretch();

    auto* scroll = new QScrollArea;
    scroll->setObjectName("WfEditScroll");
    scroll->setWidget(m_varContainer);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));

    // ── Root ──────────────────────────────────────────────────────────────────
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(headerBar);
    root->addWidget(scroll, 1);

    // ── Add variable menu ─────────────────────────────────────────────────────
    connect(addBtn, &QPushButton::clicked, this, [this, addBtn]() {
        QMenu menu(this);
        menu.addAction("Seed",        this, [this]() { addVariable(core::WorkflowVarType::Seed); });
        menu.addAction("String",      this, [this]() { addVariable(core::WorkflowVarType::String); });
        menu.addAction("Integer",     this, [this]() { addVariable(core::WorkflowVarType::Integer); });
        menu.addAction("Float",       this, [this]() { addVariable(core::WorkflowVarType::Float); });
        menu.addAction("Dir Search",  this, [this]() { addVariable(core::WorkflowVarType::DirSearch); });
        menu.addAction("Latent Size", this, [this]() { addVariable(core::WorkflowVarType::LatentSize); });
        menu.exec(addBtn->mapToGlobal(QPoint(0, addBtn->height())));
    });

    rebuildVarList();
}

void WorkflowEditPage::setWorkflowManager(core::WorkflowManager* wm, const QString& savePath)
{
    m_wm       = wm;
    m_savePath = savePath;
    refresh();
}

void WorkflowEditPage::refresh()
{
    if (m_wm) {
        const core::WorkflowFile* wf = m_wm->selectedFile();
        m_titleLabel->setText(wf ? wf->name : "No workflow selected");
    } else {
        m_titleLabel->setText("No workflow selected");
    }
    rebuildVarList();
}

void WorkflowEditPage::save()
{
    if (m_wm && !m_savePath.isEmpty())
        m_wm->saveToFile(m_savePath);
}

void WorkflowEditPage::addVariable(core::WorkflowVarType type)
{
    if (!m_wm) return;
    core::WorkflowVar var;
    var.type        = type;
    var.placeholder = "__NEW__";
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

    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(12, 10, 12, 12);
    cardLayout->setSpacing(8);

    // ── Header row: placeholder name | type badge | remove ───────────────────
    auto* headerRow = new QHBoxLayout;
    headerRow->setSpacing(6);

    auto* placeholderEdit = new QLineEdit(var.placeholder);
    placeholderEdit->setObjectName("WfPlaceholderEdit");
    placeholderEdit->setPlaceholderText("__PLACEHOLDER__");
    connect(placeholderEdit, &QLineEdit::editingFinished, this, [this, index, placeholderEdit]() {
        if (!m_wm || index >= m_wm->variables().size()) return;
        m_wm->variables()[index].placeholder = placeholderEdit->text().trimmed();
        save();
    });

    static const char* kTypeNames[] = { "Seed", "String", "Integer", "Float", "Dir Search", "Latent Size" };
    auto* typeLabel = new QLabel(kTypeNames[int(var.type)]);
    typeLabel->setObjectName("WfTypeLabel");

    auto* removeBtn = new QPushButton("×");
    removeBtn->setObjectName("WfRemoveBtn");
    removeBtn->setFixedSize(20, 20);
    removeBtn->setCursor(Qt::PointingHandCursor);
    connect(removeBtn, &QPushButton::clicked, this, [this, index]() {
        removeVariable(index);
    });

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
        addRadio("Fixed",     core::SeedBehavior::Fixed);
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
        extEdit->setPlaceholderText("Ext. whitelist: .safetensors, .ckpt  (empty = all except .sha256)");
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
            const QString dir    = dirEdit->text().trimmed();
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
                    if (!ext.isEmpty())
                        extWhitelist.insert(ext.startsWith('.') ? ext : '.' + ext);
                }
            }

            const QDir base(dir);
            const QString curSel = (m_wm && index < m_wm->variables().size())
                                 ? m_wm->variables()[index].selectedFile : QString();
            QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
            int n = 0;
            while (it.hasNext() && n < 500) {
                it.next();
                const QString ext = QFileInfo(it.fileName()).suffix().toLower().prepend('.');

                if (extWhitelist.isEmpty()) {
                    if (ext == ".sha256") continue; // default blacklist
                } else {
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

        connect(browseBtn, &QPushButton::clicked, this,
            [this, index, dirEdit, scanDir]() {
                const QString dir = QFileDialog::getExistingDirectory(
                    this, "Select Directory", dirEdit->text());
                if (dir.isEmpty()) return;
                dirEdit->setText(dir);
                if (m_wm && index < m_wm->variables().size()) {
                    m_wm->variables()[index].searchDir = dir;
                    save();
                }
                scanDir();
            });

        connect(dirEdit, &QLineEdit::editingFinished, this,
            [this, index, dirEdit, scanDir]() {
                if (m_wm && index < m_wm->variables().size()) {
                    m_wm->variables()[index].searchDir = dirEdit->text().trimmed();
                    save();
                }
                scanDir();
            });

        connect(extEdit, &QLineEdit::editingFinished, this,
            [this, index, extEdit, scanDir]() {
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

        if (!var.searchDir.isEmpty())
            scanDir();
        break;
    }

    case core::WorkflowVarType::LatentSize: {
        const QString sizesPath = utils::BASE_PATH + "/" + utils::LATENT_SIZES_PATH;
        const QList<core::LatentSizeEntry> sizes =
            core::WorkflowManager::loadLatentSizes(sizesPath);

        auto* bodyRow = new QHBoxLayout;
        bodyRow->setSpacing(8);
        bodyRow->setAlignment(Qt::AlignTop);

        auto* sizeList   = new QListWidget;
        sizeList->setObjectName("WfFileList");
        sizeList->setFixedHeight(150);
        sizeList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        sizeList->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));

        auto* ratioPreview = new RatioPreview;

        if (sizes.isEmpty()) {
            auto* hint = new QListWidgetItem(
                QString("File not found: %1").arg(utils::LATENT_SIZES_PATH));
            hint->setFlags(Qt::NoItemFlags);
            QFont f = hint->font(); f.setItalic(true); hint->setFont(f);
            sizeList->addItem(hint);
        } else {
            for (const core::LatentSizeEntry& e : sizes) {
                auto* item = new QListWidgetItem(e.label);
                item->setData(Qt::UserRole,     e.w);
                item->setData(Qt::UserRole + 1, e.h);
                item->setData(Qt::UserRole + 2, e.label);
                const bool sel = (e.label == var.stringValue);
                if (sel) {
                    QFont f = item->font(); f.setBold(true); item->setFont(f);
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
                    QFont f = it->font(); f.setBold(sel); it->setFont(f);
                    it->setForeground(QColor(sel ? "#5a9a5a" : "#666666"));
                }
            });

        bodyRow->addWidget(sizeList, 1);
        bodyRow->addWidget(ratioPreview, 0, Qt::AlignTop);
        cardLayout->addLayout(bodyRow);
        break;
    }

    } // switch

    return card;
}

} // namespace gui
