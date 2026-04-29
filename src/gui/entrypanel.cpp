#include <gui/entrypanel.h>
#include <utils/appconfig.h>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QCryptographicHash>
#include <QFutureWatcher>
#include <QtConcurrent>

#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QCursor>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <functional>

using namespace core;
using namespace model;
using namespace utils;

namespace {

class LoraDropZone : public QLabel {
public:
    std::function<void(const QString&)> onFileDropped;
    explicit LoraDropZone(QWidget* parent = nullptr) : QLabel(parent) {
        setAcceptDrops(true);
        setCursor(Qt::PointingHandCursor);
        setAlignment(Qt::AlignCenter);
        setObjectName("LoraDropZone");
        setWordWrap(false);
    }
protected:
    void dragEnterEvent(QDragEnterEvent* e) override {
        if (!e->mimeData()->hasUrls()) return;
        const QString ext = QFileInfo(e->mimeData()->urls().first().toLocalFile()).suffix().toLower();
        if (ext == "safetensors" || ext == "ckpt" || ext == "pt" || ext == "pth")
            e->acceptProposedAction();
    }
    void dropEvent(QDropEvent* e) override {
        const QString path = e->mimeData()->urls().first().toLocalFile();
        if (!path.isEmpty() && onFileDropped) onFileDropped(path);
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            const QString path = QFileDialog::getOpenFileName(
                this, "Select LoRA",  {},
                "Model files (*.safetensors *.ckpt *.pt *.pth)");
            if (!path.isEmpty() && onFileDropped) onFileDropped(path);
        }
        QLabel::mousePressEvent(e);
    }
};


static QColor categoryColor(int cat)
{
    switch (cat) {
    case 0:  return { 0xb4, 0xc7, 0xd9 }; // general
    case 1:  return { 0xf2, 0xac, 0x08 }; // artist
    case 3:  return { 0xdd, 0x00, 0xdd }; // copyright
    case 4:  return { 0x00, 0xaa, 0x00 }; // character
    case 5:  return { 0xaa, 0xaa, 0xaa }; // meta
    default: return { 0x60, 0x60, 0x60 };
    }
}

static constexpr int SAVE_W = 1024;
static constexpr int SAVE_H = 1280;

static QString resolveImagePath(const Entry* e, int idx)
{
    return BASE_PATH + "/data/entry/" + e->uuid + "/" + e->images[idx].fileName;
}

static void saveResized(const QString& src, const QString& dst)
{
    QImage img(src);
    if (img.isNull()) { QFile::copy(src, dst); return; }
    QImage scaled = img.scaled(SAVE_W, SAVE_H, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    if (scaled.width() > SAVE_W || scaled.height() > SAVE_H) {
        const int x = (scaled.width()  - SAVE_W) / 2;
        const int y = (scaled.height() - SAVE_H) / 2;
        scaled = scaled.copy(x, y, SAVE_W, SAVE_H);
    }
    scaled.save(dst, "PNG");
}

} // anonymous namespace


namespace gui {

EntryPanel::EntryPanel(EntryModel* model, QWidget* parent)
    : QWidget(parent), m_model(model)
{
    setObjectName("EntryPanel");
    setAttribute(Qt::WA_StyledBackground, true);

    // ── Image dropper ─────────────────────────────────────────────────────────
    m_imageDrop = new ImageDropper(this);
    connect(m_imageDrop, &ImageDropper::imageDropped, this, [this](const QString& srcPath) {
        if (!m_entry) return;

        // If no slots exist yet, create the first one
        if (m_entry->images.isEmpty()) {
            core::ImageData slot;
            slot.fileName = "00001.png";
            m_entry->images << slot;
            m_imageIdx = 0;
            m_searchBar->setEnabled(true);
            m_removeImageBtn->setEnabled(true);
            m_pageLabel->setText("1 / 1");
            m_prevBtn->setEnabled(false);
            m_nextBtn->setEnabled(false);
            rebuildTagList();
        }

        // Replace / set the file for the current slot
        const QString entryDir = BASE_PATH + "/data/entry/" + m_entry->uuid;
        QDir().mkpath(entryDir);
        const QString dest = entryDir + "/" + m_entry->images[m_imageIdx].fileName;
        if (QFileInfo(srcPath).canonicalFilePath() == QFileInfo(dest).canonicalFilePath()) return;
        QFile::remove(dest);
        saveResized(srcPath, dest);
        m_imageDrop->setImage(srcPath);
        m_model->saveEntry(m_entry->id);
        emit entryModified(m_entry->id);
    });

    // ── Title ─────────────────────────────────────────────────────────────────
    m_titleEdit = new QLineEdit(this);
    m_titleEdit->setObjectName("EntryTitle");
    m_titleEdit->setPlaceholderText("—");
    connect(m_titleEdit, &QLineEdit::returnPressed, this, [this]() {
        if (!m_entry) return;
        m_entry->title = m_titleEdit->text().trimmed();
        m_model->saveEntry(m_entry->id);
        emit entryModified(m_entry->id);
    });
    connect(m_titleEdit, &QLineEdit::editingFinished, this, [this]() {
        if (!m_entry) return;
        m_titleEdit->setText(m_entry->title); // revert on focus-loss without Enter
    });

    // ── Action buttons ────────────────────────────────────────────────────────
    m_composerBtn  = new QPushButton("Composer Toggle", this);
    m_composerBtn->setCheckable(true);
    m_copyBtn      = new QPushButton("Copy entry tags", this);
    m_deleteBtn    = new QPushButton("Delete entry", this);

    m_composerBtn->setObjectName("EntryActionBtn");
    m_copyBtn->setObjectName("EntryActionBtn");
    m_deleteBtn->setObjectName("EntryActionBtnDelete");
    connect(m_copyBtn, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_activeTags.values().join(", "));
    });
    connect(m_deleteBtn, &QPushButton::clicked, this, [this]() {
        if (!m_entry) return;
        m_model->deleteEntry(m_entry->id);
        setEntry(nullptr);
        emit entryListChanged();
    });

    // ── Comment field ─────────────────────────────────────────────────────────
    m_commentEdit = new QPlainTextEdit(this);
    m_commentEdit->setObjectName("EntryComment");
    m_commentEdit->setPlaceholderText("Notes...");
    m_commentEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_commentEdit->setFixedHeight(76);

    m_commentTimer = new QTimer(this);
    m_commentTimer->setSingleShot(true);
    m_commentTimer->setInterval(600);

    connect(m_commentEdit, &QPlainTextEdit::textChanged, this, [this]() {
        if (!m_entry) return;
        m_entry->comment = m_commentEdit->toPlainText();
        m_commentTimer->start();
    });
    connect(m_commentTimer, &QTimer::timeout, this, [this]() {
        if (m_entry) m_model->saveEntry(m_entry->id);
    });

    // ── Image pager ───────────────────────────────────────────────────────────
    m_prevBtn        = new QPushButton("◄", this);
    m_nextBtn        = new QPushButton("►", this);
    m_addImageBtn    = new QPushButton("+", this);
    m_removeImageBtn = new QPushButton("−", this);
    m_pageLabel      = new QLabel("",       this);
    m_prevBtn       ->setObjectName("EntryPageBtn");
    m_nextBtn       ->setObjectName("EntryPageBtn");
    m_addImageBtn   ->setObjectName("EntryPageBtn");
    m_removeImageBtn->setObjectName("EntryPageBtnRemove");
    m_pageLabel     ->setObjectName("EntryPageLabel");
    m_prevBtn       ->setFixedSize(24, 24);
    m_nextBtn       ->setFixedSize(24, 24);
    m_addImageBtn   ->setFixedSize(24, 24);
    m_removeImageBtn->setFixedSize(24, 24);
    m_pageLabel->setAlignment(Qt::AlignCenter);

    connect(m_composerBtn, &QPushButton::clicked, this, [this]() {
        if (!m_entry || m_imageIdx >= m_entry->images.size())
            return;
        emit tagsExported(int(m_entry->id), m_imageIdx, m_activeTags.values());
    });

    connect(m_prevBtn, &QPushButton::clicked, this, [this]() {
        if (m_entry && m_imageIdx > 0) loadImagePage(m_imageIdx - 1);
    });
    connect(m_nextBtn, &QPushButton::clicked, this, [this]() {
        if (m_entry && m_imageIdx < m_entry->images.size() - 1)
            loadImagePage(m_imageIdx + 1);
    });
    connect(m_addImageBtn, &QPushButton::clicked, this, [this]() {
        if (!m_entry) return;
        addEmptyImageSlot();
    });
    connect(m_removeImageBtn, &QPushButton::clicked, this, [this]() {
        if (!m_entry || m_entry->images.isEmpty()) return;
        const int removedIdx = m_imageIdx;
        m_model->removeImageFromEntry(m_entry->id, removedIdx);

        if (m_entry->images.isEmpty()) {
            // Last image removed — fall back to empty state
            m_imageIdx = 0;
            m_imageDrop->clearImage();
            m_pageLabel->setText("—");
            m_prevBtn->setEnabled(false);
            m_nextBtn->setEnabled(false);
            m_removeImageBtn->setEnabled(false);
            m_searchBar->setEnabled(false);
            rebuildTagList();
        } else {
            // Navigate to the slot that now occupies this position (or the one before)
            loadImagePage(std::min(removedIdx, (int)m_entry->images.size() - 1));
        }
        emit entryModified(m_entry->id);
    });

    // ── Header layout: [image + pager] | [title + buttons] ───────────────────
    auto* pagerRow = new QHBoxLayout;
    pagerRow->addWidget(m_prevBtn);
    pagerRow->addWidget(m_pageLabel, 1);
    pagerRow->addWidget(m_nextBtn);
    pagerRow->addWidget(m_addImageBtn);
    pagerRow->addWidget(m_removeImageBtn);

    auto* leftCol = new QVBoxLayout;
    leftCol->setSpacing(4);
    leftCol->addWidget(m_imageDrop, 0, Qt::AlignTop | Qt::AlignHCenter);
    leftCol->addLayout(pagerRow);

    // ── LoRA section ──────────────────────────────────────────────────────────
    auto* loraDropZone = new LoraDropZone(this);
    loraDropZone->setText("Drop .safetensors / .ckpt");
    m_loraFileLabel = loraDropZone;

    loraDropZone->onFileDropped = [this](const QString& path) {
        if (!m_entry) return;
        core::Entry* targetEntry = m_entry;

        emit statusMessageRequested("Computing LoRA hash...");

        auto* watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcher<QString>::finished, this,
            [this, watcher, path, targetEntry]() {
                watcher->deleteLater();
                const QString hash = watcher->result();

                // Verify the entry is still loaded and valid
                if (m_model->entryById(targetEntry->id) != targetEntry) return;

                if (!hash.isEmpty()) {
                    core::Entry* existing = m_model->entryByLoraSha256(hash);
                    if (existing && existing != targetEntry) {
                        emit statusMessageRequested(
                            QString("LoRA already assigned to \"%1\"").arg(existing->title));
                        return;
                    }
                }

                if (!targetEntry->lora.has_value())
                    targetEntry->lora = core::LoraConfig{};
                targetEntry->lora->file   = path;
                targetEntry->lora->sha256 = hash;
                m_model->saveEntry(targetEntry->id);

                if (m_entry == targetEntry) refreshLoraSection();
                emit statusMessageRequested(
                    QString("LoRA assigned: %1").arg(QFileInfo(path).fileName()));
            });
        watcher->setFuture(QtConcurrent::run([path]() -> QString {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) return {};
            QCryptographicHash h(QCryptographicHash::Sha256);
            h.addData(&f);
            return h.result().toHex();
        }));
    };

    m_loraClearBtn = new QPushButton("Clear", this);
    m_loraClearBtn->setObjectName("EntryActionBtn");
    m_loraClearBtn->setFixedHeight(22);
    m_loraClearBtn->setEnabled(false);
    connect(m_loraClearBtn, &QPushButton::clicked, this, [this]() {
        if (!m_entry) return;
        const int32_t id = m_entry->id;
        m_entry->lora.reset();
        m_model->saveEntry(id);
        refreshLoraSection();
        emit loraCleared(id);
    });

    m_loraSha256Btn = new QPushButton("SHA256", this);
    m_loraSha256Btn->setObjectName("EntryActionBtn");
    m_loraSha256Btn->setFixedHeight(22);
    m_loraSha256Btn->setEnabled(false);
    connect(m_loraSha256Btn, &QPushButton::clicked, this, [this]() {
        if (!m_entry || !m_entry->lora.has_value() || m_entry->lora->sha256.isEmpty()) return;
        QApplication::clipboard()->setText(m_entry->lora->sha256);
        emit statusMessageRequested("SHA256 copied to clipboard");
    });

    auto makeSpinBox = [](double min, double max, double step, double val) {
        auto* s = new QDoubleSpinBox;
        s->setRange(min, max);
        s->setSingleStep(step);
        s->setDecimals(2);
        s->setValue(val);
        s->setFixedWidth(64);
        s->setObjectName("LoraSpinBox");
        return s;
    };
    m_loraModelStr = makeSpinBox(0.0, 2.0, 0.05, 0.9);
    m_loraClipStr  = makeSpinBox(0.0, 4.0, 0.1,  2.0);

    m_loraTimer = new QTimer(this);
    m_loraTimer->setSingleShot(true);
    m_loraTimer->setInterval(500);
    connect(m_loraTimer, &QTimer::timeout, this, [this]() {
        if (!m_entry || !m_entry->lora.has_value()) return;
        m_entry->lora->modelStr = m_loraModelStr->value();
        m_entry->lora->clipStr  = m_loraClipStr->value();
        m_model->saveEntry(m_entry->id);
    });

    auto onSpinChanged = [this](double) { m_loraTimer->start(); };
    connect(m_loraModelStr, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, onSpinChanged);
    connect(m_loraClipStr,  QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, onSpinChanged);

    auto* loraHeaderRow = new QHBoxLayout;
    loraHeaderRow->setContentsMargins(0, 4, 0, 0);
    loraHeaderRow->setSpacing(6);
    auto* loraLabel = new QLabel("LoRA", this);
    loraLabel->setObjectName("EntrySubLabel");
    loraHeaderRow->addWidget(loraLabel);
    loraHeaderRow->addStretch();
    loraHeaderRow->addWidget(m_loraSha256Btn);
    loraHeaderRow->addWidget(m_loraClearBtn);

    auto* loraSpinRow = new QHBoxLayout;
    loraSpinRow->setSpacing(4);
    auto makeSpinLabel = [this](const QString& t) {
        auto* l = new QLabel(t, this);
        l->setObjectName("LoraSpinLabel");
        return l;
    };
    loraSpinRow->addWidget(makeSpinLabel("Model"));
    loraSpinRow->addWidget(m_loraModelStr);
    loraSpinRow->addSpacing(4);
    loraSpinRow->addWidget(makeSpinLabel("Clip"));
    loraSpinRow->addWidget(m_loraClipStr);
    loraSpinRow->addStretch();

    m_loraSection = new QWidget(this);
    m_loraSection->setObjectName("LoraSection");
    auto* loraSectionLayout = new QVBoxLayout(m_loraSection);
    loraSectionLayout->setContentsMargins(0, 0, 0, 0);
    loraSectionLayout->setSpacing(2);
    loraSectionLayout->addLayout(loraHeaderRow);
    loraSectionLayout->addWidget(loraDropZone);
    loraSectionLayout->addLayout(loraSpinRow);

    auto* rightCol = new QVBoxLayout;
    rightCol->setSpacing(4);
    rightCol->addWidget(m_titleEdit);
    rightCol->addSpacing(6);
    rightCol->addWidget(m_composerBtn);
    rightCol->addWidget(m_copyBtn);
    rightCol->addWidget(m_deleteBtn);
    rightCol->addWidget(m_commentEdit);
    rightCol->addWidget(m_loraSection);
    rightCol->addStretch();

    auto* headerRow = new QHBoxLayout;
    headerRow->setSpacing(10);
    headerRow->addLayout(leftCol);
    headerRow->addLayout(rightCol, 1);

    m_headerWidget = new QWidget;
    m_headerWidget->setObjectName("EntryHeader");
    auto* headerLayout = new QVBoxLayout(m_headerWidget);
    headerLayout->setContentsMargins(10, 10, 10, 10);
    headerLayout->addLayout(headerRow);

    // ── Tag search bar ────────────────────────────────────────────────────────
    m_searchBar = new TagSearchBar(this);
    m_searchBar->setActiveTags(&m_activeTags);

    connect(m_searchBar, &TagSearchBar::tagAdded, this, [this](const QString& tag) {
        if (!m_entry) return;
        m_activeTags.insert(tag);
        addTagRowToList(tag);
        emit entryTagAdded(m_entry->id, m_imageIdx, tag);
        updateExtraBtnState();
    });
    connect(m_searchBar, &TagSearchBar::tagAlreadyPresent,
            this, &EntryPanel::tagAlreadyPresent);
    connect(m_searchBar, &TagSearchBar::queryChanged, this, [this](const QString& text) {
        const QString lower = text.toLower();
        for (int i = 0; i < m_tagListLayout->count(); ++i) {
            QWidget* w = m_tagListLayout->itemAt(i)->widget();
            if (!w) continue;
            const QString tag = w->property("_tag").toString();
            if (tag.isEmpty()) continue;
            w->setVisible(lower.isEmpty() || tag.contains(lower));
        }
    });

    // ── Tag list (scrollable) ─────────────────────────────────────────────────
    m_tagListContainer = new QWidget;
    m_tagListContainer->setObjectName("EntryTagList");
    m_tagListLayout = new QVBoxLayout(m_tagListContainer);
    m_tagListLayout->setContentsMargins(0, 0, 0, 0);
    m_tagListLayout->setSpacing(1);
    m_tagListLayout->addStretch();

    m_tagScroll = new QScrollArea;
    m_tagScroll->setObjectName("EntryTagScroll");
    m_tagScroll->setWidget(m_tagListContainer);
    m_tagScroll->setWidgetResizable(true);
    m_tagScroll->setWidgetResizable(true);
    m_tagScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tagScroll->setFrameShape(QFrame::NoFrame);

    m_tagsWidget = new QWidget;
    m_tagsWidget->setObjectName("EntryTagsSection");
    auto* tagsLayout = new QVBoxLayout(m_tagsWidget);
    tagsLayout->setContentsMargins(8, 8, 8, 8);
    tagsLayout->setSpacing(6);
    tagsLayout->addWidget(m_searchBar);
    tagsLayout->addWidget(m_tagScroll, 1);

    // ── Root content layout (direction flips on orientation change) ───────────
    m_contentWidget = new QWidget;
    m_rootLayout = new QBoxLayout(QBoxLayout::TopToBottom, m_contentWidget);
    m_rootLayout->setContentsMargins(0, 0, 0, 0);
    m_rootLayout->setSpacing(0);
    m_rootLayout->addWidget(m_headerWidget);
    m_rootLayout->addWidget(m_tagsWidget, 1);

    // ── Empty state ───────────────────────────────────────────────────────────
    auto* emptyLabel = new QLabel("Select an entry\nto view details", this);
    emptyLabel->setObjectName("EntryEmptyLabel");
    emptyLabel->setAlignment(Qt::AlignCenter);

    for (QWidget* w : QList<QWidget*>{ emptyLabel, m_contentWidget })
    {
        w->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(w, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
            QMenu menu(this);
            QAction* act = menu.addAction("Add entry");
            if (menu.exec(mapToGlobal(pos)) != act) return;
            AddEntryDialog dlg(window());
            if (dlg.exec() != QDialog::Accepted) return;
            core::Entry entry = dlg.buildEntry();
            entry.images.append(core::ImageData{ "00001.png", {} });
            m_model->addEntry(std::move(entry));
            emit entryListChanged();
        });
    }

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(emptyLabel);       // 0 — no selection
    m_stack->addWidget(m_contentWidget);  // 1 — entry loaded


    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);
    outerLayout->addWidget(m_stack, 1);

    applyOrientation(false);
}

// ── Public API ────────────────────────────────────────────────────────────────

void EntryPanel::setDanbooruIndex(core::DanbooruIndex* index)
{
    m_danbooruIndex = index;
    m_searchBar->setIndex(index);
}

void EntryPanel::setActiveGroups(const QMap<int, QList<int>>& groups)
{
    m_activeGroups = groups;
    updateExtraBtnState();
}

void EntryPanel::setEntry(core::Entry* entry)
{
    m_entry = entry;
    if (!entry) {
        m_stack->setCurrentIndex(0);
        return;
    }
    m_stack->setCurrentIndex(1);
    m_titleEdit->setText(entry->title);
    m_commentEdit->blockSignals(true);
    m_commentEdit->setPlainText(entry->comment);
    m_commentEdit->blockSignals(false);
    refreshLoraSection();
    if (entry->images.isEmpty()) {
        m_imageIdx = 0;
        m_imageDrop->clearImage();
        m_pageLabel->setText("—");
        m_prevBtn->setEnabled(false);
        m_nextBtn->setEnabled(false);
        m_removeImageBtn->setEnabled(false);
        m_searchBar->setEnabled(false);
        rebuildTagList();
    } else {
        loadImagePage(0);
    }
}

void EntryPanel::applyOrientation(bool portrait)
{
    if (portrait) {
        // Panel is short+wide — place header left, tags right
        m_rootLayout->setDirection(QBoxLayout::LeftToRight);
        m_headerWidget->setFixedWidth(300);
        m_headerWidget->setMaximumHeight(QWIDGETSIZE_MAX);
        m_headerWidget->setMinimumHeight(0);
        m_imageDrop->setFixedSize(120, 154);
    } else {
        // Panel is tall+narrow — stack header above tags
        m_rootLayout->setDirection(QBoxLayout::TopToBottom);
        m_headerWidget->setMaximumWidth(QWIDGETSIZE_MAX);
        m_headerWidget->setMinimumWidth(0);
        m_headerWidget->setMaximumHeight(QWIDGETSIZE_MAX);
        m_imageDrop->setFixedSize(200, 257);
    }
}

// ── Private ───────────────────────────────────────────────────────────────────

void EntryPanel::loadImagePage(int idx)
{
    m_imageIdx = idx;
    const QString path = resolveImagePath(m_entry, idx);
    if (QFile::exists(path))
        m_imageDrop->setImage(path);
    else
        m_imageDrop->clearImage();
    m_searchBar->setEnabled(true);
    m_removeImageBtn->setEnabled(true);

    const int total = m_entry->images.size();
    m_pageLabel->setText(QString("%1 / %2").arg(idx + 1).arg(total));
    m_prevBtn->setEnabled(idx > 0);
    m_nextBtn->setEnabled(idx < total - 1);

    rebuildTagList();
}

void EntryPanel::rebuildTagList()
{
    while (m_tagListLayout->count() > 0) {
        QLayoutItem* item = m_tagListLayout->takeAt(0);
        if (QWidget* w = item->widget()) delete w;
        delete item;
    }
    m_activeTags.clear();

    if (!m_entry || m_imageIdx >= m_entry->images.size()) {
        m_tagListLayout->addStretch();
        updateExtraBtnState();
        return;
    }

    const QList<QString> tags = m_model->getTags(m_entry->images[m_imageIdx].tagIds);
    for (const QString& tag : tags) {
        m_activeTags.insert(tag);
        m_tagListLayout->addWidget(createTagRow(tag));
    }
    m_tagListLayout->addStretch();
    updateExtraBtnState();
}

void EntryPanel::updateExtraBtnState()
{
    if (!m_entry) { m_composerBtn->setChecked(false); return; }
    const auto& imgs = m_activeGroups.value(int(m_entry->id));
    m_composerBtn->setChecked(imgs.contains(m_imageIdx));
}

QWidget* EntryPanel::createTagRow(const QString& tag)
{
    const int    cat = m_danbooruIndex ? m_danbooruIndex->tagCategory(tag) : -1;
    const QColor col = categoryColor(cat);

    auto* row = new QWidget(m_tagListContainer);
    row->setObjectName("TagRow");
    row->setProperty("_tag", tag);

    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(8, 2, 8, 2);
    rl->setSpacing(6);

    // Category dot
    auto* dot = new QWidget(row);
    dot->setFixedSize(8, 8);
    dot->setStyleSheet(QString("background:%1;border-radius:4px;").arg(col.name()));
    rl->addWidget(dot, 0, Qt::AlignVCenter);

    // Editable tag name
    auto* edit = new QLineEdit(tag, row);
    edit->setObjectName("TagLabel");
    edit->setFrame(false);
    edit->setStyleSheet(
        QString("QLineEdit { color:%1; font-size:12px; background:transparent; border:none; padding:0; }")
        .arg(col.name()));
    rl->addWidget(edit, 1);

    // returnPressed: commit the rename
    connect(edit, &QLineEdit::returnPressed, this, [this, edit, dot, row]() {
        const QString oldTag = row->property("_tag").toString();
        const QString newTag = edit->text().trimmed();
        if (newTag.isEmpty()) { edit->setText(oldTag); return; }
        if (newTag == oldTag) return;
        if (m_activeTags.contains(newTag)) { edit->setText(oldTag); return; }

        // Update dot and text color for new tag's category
        const int    newCat = m_danbooruIndex ? m_danbooruIndex->tagCategory(newTag) : -1;
        const QColor newCol = categoryColor(newCat);
        dot->setStyleSheet(QString("background:%1;border-radius:4px;").arg(newCol.name()));
        edit->setStyleSheet(
            QString("QLineEdit { color:%1; font-size:12px; background:transparent; border:none; padding:0; }")
            .arg(newCol.name()));

        row->setProperty("_tag", newTag);
        m_activeTags.remove(oldTag);
        m_activeTags.insert(newTag);

        if (m_entry) {
            emit entryTagRemoved(m_entry->id, m_imageIdx, oldTag);
            emit entryTagAdded(m_entry->id, m_imageIdx, newTag);
        }
        updateExtraBtnState();
    });

    // editingFinished (focus-loss without Enter): revert to committed tag
    connect(edit, &QLineEdit::editingFinished, this, [edit, row]() {
        edit->setText(row->property("_tag").toString());
    });

    // Remove button — reads current tag from row property so it works after rename
    auto* del = new QPushButton("×", row);
    del->setObjectName("TagRemoveBtn");
    del->setFixedSize(18, 18);
    del->setCursor(Qt::PointingHandCursor);
    connect(del, &QPushButton::clicked, this, [this, row]() {
        const QString t = row->property("_tag").toString();
        m_activeTags.remove(t);
        row->deleteLater();
        if (m_entry) emit entryTagRemoved(m_entry->id, m_imageIdx, t);
        updateExtraBtnState();
    });
    rl->addWidget(del);

    row->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(row, &QWidget::customContextMenuRequested, this,
        [this, row](const QPoint&) {
            const QString t = row->property("_tag").toString();
            QMenu menu;
            QAction* wikiAct   = menu.addAction("Go to Wiki");
            QAction* facetAct  = menu.addAction("Edit facets");
            QAction* deleteAct = menu.addAction("Remove tag");
            QAction* chosen    = menu.exec(QCursor::pos());
            if (chosen == wikiAct) {
                emit wikiRequested(t);
            } else if (chosen == facetAct) {
                emit facetEditorRequested(t);
            } else if (chosen == deleteAct) {
                m_activeTags.remove(t);
                row->deleteLater();
                if (m_entry) emit entryTagRemoved(m_entry->id, m_imageIdx, t);
                updateExtraBtnState();
            }
        });

    return row;
}

void EntryPanel::addTagRowToList(const QString& tag)
{
    // Insert before the trailing stretch
    const int pos = std::max(0, m_tagListLayout->count() - 1);
    m_tagListLayout->insertWidget(pos, createTagRow(tag));
}
    
void EntryPanel::refreshLoraSection()
{
    if (!m_entry || !m_entry->lora.has_value()) {
        m_loraFileLabel->setText("Drop .safetensors / .ckpt");
        m_loraModelStr->blockSignals(true);
        m_loraClipStr->blockSignals(true);
        m_loraModelStr->setValue(0.9);
        m_loraClipStr->setValue(2.0);
        m_loraModelStr->blockSignals(false);
        m_loraClipStr->blockSignals(false);
        m_loraModelStr->setEnabled(false);
        m_loraClipStr->setEnabled(false);
        m_loraClearBtn->setEnabled(false);
        m_loraSha256Btn->setEnabled(false);
    } else {
        const auto& lc = *m_entry->lora;
        m_loraFileLabel->setText(QFileInfo(lc.file).fileName());
        m_loraModelStr->blockSignals(true);
        m_loraClipStr->blockSignals(true);
        m_loraModelStr->setValue(lc.modelStr);
        m_loraClipStr->setValue(lc.clipStr);
        m_loraModelStr->blockSignals(false);
        m_loraClipStr->blockSignals(false);
        m_loraModelStr->setEnabled(true);
        m_loraClipStr->setEnabled(true);
        m_loraClearBtn->setEnabled(true);
        m_loraSha256Btn->setEnabled(!lc.sha256.isEmpty());
    }
}

void EntryPanel::addEmptyImageSlot()
{
    if (!m_entry) return;

    int maxNum = 0;
    for (const auto& img : m_entry->images) {
        bool ok;
        const int n = QFileInfo(img.fileName).completeBaseName().toInt(&ok);
        if (ok && n > maxNum) maxNum = n;
    }
    const QString fileName = QString("%1.png").arg(maxNum + 1, 5, 10, QChar('0'));

    core::ImageData imgData;
    imgData.fileName = fileName;
    m_entry->images << imgData;

    m_model->saveEntry(m_entry->id);
    loadImagePage(m_entry->images.size() - 1);
    emit entryModified(m_entry->id);
}

} // namespace gui
