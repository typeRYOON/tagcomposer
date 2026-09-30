#include <app/prompt_history_page.h>
#include <app/app_scroll_bar.h>
#include <app/comfy_client.h>
#include <app/composer_page.h>
#include <core/entry_store.h>
#include <core/prompt_history.h>
#include <core/workflow_io.h>
#include <QDateTime>
#include <QEvent>
#include <QFrame>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

constexpr int kTileWidth = 120;
constexpr int kTileHeight = 150;

// A faithful short rendering of what was actually sent for each var type.
QString formatVarValue(const WorkflowVar& var)
{
    return std::visit(
        [](auto&& value) -> QString {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, SeedVar>) {
                return QString::number(value.value);
            } else if constexpr (std::is_same_v<T, StringVar>) {
                return value.value.isEmpty() ? u"(empty)"_s : value.value;
            } else if constexpr (std::is_same_v<T, IntVar>) {
                return QString::number(value.value);
            } else if constexpr (std::is_same_v<T, FloatVar>) {
                return QString::number(value.value, 'f', 4);
            } else if constexpr (std::is_same_v<T, DirSearchVar>) {
                return value.selectedFile.isEmpty() ? u"(none)"_s : value.selectedFile;
            } else if constexpr (std::is_same_v<T, LatentSizeVar>) {
                return u"%1x%2"_s.arg(value.width).arg(value.height);
            } else if constexpr (std::is_same_v<T, ImageVar>) {
                return value.imageUuid.isEmpty() ? u"(none)"_s
                                                 : value.imageUuid.left(8) + u"..."_s;
            } else {
                return u"[%1 wildcard line(s)]"_s.arg(value.bundles.size());
            }
        },
        var.value);
}

QString formatLora(const Lora& lora)
{
    return u"%1  -  model %2 / clip %3"_s.arg(lora.file.isEmpty() ? u"(empty)"_s : lora.file)
        .arg(lora.modelStrength, 0, 'f', 2)
        .arg(lora.clipStrength, 0, 'f', 2);
}

QLabel* sectionLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(u"PhSectionLabel"_s);
    return label;
}

QLabel* dimText(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(u"PhDimText"_s);
    return label;
}

// Decode, scale and centre-crop. A pure function, so it can run on any
// thread without touching a single Qt GUI object.
QImage decodeThumb(const QString& path)
{
    const QImage source(path);
    if (source.isNull()) return {};

    const QImage scaled = source.scaled(kTileWidth, kTileHeight,
                                        Qt::KeepAspectRatioByExpanding,
                                        Qt::SmoothTransformation);
    return scaled.copy(std::max(0, (scaled.width() - kTileWidth) / 2),
                       std::max(0, (scaled.height() - kTileHeight) / 2), kTileWidth,
                       kTileHeight);
}

QPixmap placeholderThumb()
{
    QPixmap pixmap(kTileWidth, kTileHeight);
    pixmap.fill(QColor(0x14, 0x14, 0x14));
    return pixmap;
}

} // namespace

PromptHistoryPage::PromptHistoryPage(PromptHistory& history, EntryStore& entries,
                                     ComposerPage& composer, ComfyClient& comfy,
                                     QWidget* parent)
    : QWidget(parent), m_history(&history), m_entries(&entries), m_composer(&composer),
      m_comfy(&comfy)
{
    setObjectName(u"PromptHistoryPage"_s);
    setAttribute(Qt::WA_StyledBackground, true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- Header, matching the workflow editor's
    auto* header = new QWidget(this);
    header->setObjectName(u"PhHeader"_s);
    header->setAttribute(Qt::WA_StyledBackground, true);
    header->setFixedHeight(50);

    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 12, 20, 12);
    headerLayout->setSpacing(16);

    auto* title = new QLabel(u"PROMPT HISTORY"_s, header);
    title->setObjectName(u"WfEditTitle"_s);

    auto* subtitle = new QLabel(u"session only - cleared on app close"_s, header);
    subtitle->setObjectName(u"WfEditSubtitle"_s);

    m_clearAllBtn = new QPushButton(u"Clear"_s, header);
    m_clearAllBtn->setObjectName(u"PhClearBtn"_s);
    m_clearAllBtn->setCursor(Qt::PointingHandCursor);

    headerLayout->addWidget(title);
    headerLayout->addWidget(subtitle, 1);
    headerLayout->addWidget(m_clearAllBtn);
    root->addWidget(header);

    // ---- The list beside the details
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(u"PhSplit"_s);
    m_splitter->setHandleWidth(2);
    m_splitter->setChildrenCollapsible(false);

    m_list = new QListWidget(this);
    m_list->setObjectName(u"PhList"_s);
    m_list->setVerticalScrollBar(new AppScrollBar(Qt::Vertical));
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setFocusPolicy(Qt::NoFocus);

    m_detailsScroll = new QScrollArea(this);
    m_detailsScroll->setObjectName(u"PhDetailsScroll"_s);
    m_detailsScroll->setWidgetResizable(true);
    m_detailsScroll->setFrameShape(QFrame::NoFrame);
    m_detailsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_detailsScroll->setVerticalScrollBar(new AppScrollBar(Qt::Vertical));

    // A viewport resize is what both a page resize and a splitter drag look
    // like from here, so the column count is recomputed from it.
    m_detailsScroll->viewport()->installEventFilter(this);

    m_detailsHost = new QWidget(m_detailsScroll);
    m_detailsHost->setObjectName(u"PhDetailsHost"_s);
    m_detailsLayout = new QVBoxLayout(m_detailsHost);
    m_detailsLayout->setContentsMargins(14, 10, 14, 10);
    m_detailsLayout->setSpacing(6);
    m_detailsScroll->setWidget(m_detailsHost);

    m_splitter->addWidget(m_list);
    m_splitter->addWidget(m_detailsScroll);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({280, 800});
    root->addWidget(m_splitter, 1);

    // Takes the splitter's slot when there is nothing to show.
    m_emptyLabel = new QLabel(u"No prompts queued this session."_s, this);
    m_emptyLabel->setObjectName(u"PhEmpty"_s);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->hide();
    root->addWidget(m_emptyLabel, 1);

    rebuildList();

    connect(m_history, &PromptHistory::recordAdded, this, [this](int) {
        // Records prepend, so the selection is read first and shifted by one
        // to keep the user pinned to the same record.
        const int previous = selectedRecordIndex();
        rebuildList();
        if (previous < 0) return;

        const int row = previous + 1;
        if (row < m_list->count()) m_list->setCurrentRow(row);
    });
    connect(m_history, &PromptHistory::cleared, this, [this]() { rebuildList(); });

    connect(m_list, &QListWidget::currentRowChanged, this,
            [this](int) { onSelectionChanged(); });
    connect(m_clearAllBtn, &QPushButton::clicked, this, [this]() { m_history->clear(); });

    onSelectionChanged();
}

void PromptHistoryPage::rebuildList()
{
    const QSignalBlocker block(m_list);
    m_list->clear();

    const QList<PromptRecord>& records = m_history->records();
    m_splitter->setVisible(!records.isEmpty());
    m_emptyLabel->setVisible(records.isEmpty());

    for (int i = 0; i < int(records.size()); ++i) {
        const PromptRecord& record = records[i];
        const qsizetype entryCount = record.snapshot.activePushes.size();

        QString label = u"%1   %2\n%3 var(s)  -  %4 LoRA(s)  -  %5 entr%6"_s
                            .arg(record.queuedAt.toString(u"HH:mm:ss"_s),
                                 record.workflowName.isEmpty() ? u"(unnamed)"_s
                                                               : record.workflowName)
                            .arg(record.snapshot.workflowVarValues.size())
                            .arg(record.lorasUsed.size())
                            .arg(entryCount)
                            .arg(entryCount == 1 ? u"y"_s : u"ies"_s);
        if (!record.batchEntryUuid.isEmpty()) label += u"  -  batch"_s;

        auto* item = new QListWidgetItem(label, m_list);
        item->setData(Qt::UserRole, i);
    }

    if (!records.isEmpty()) m_list->setCurrentRow(0);
    onSelectionChanged();
}

int PromptHistoryPage::selectedRecordIndex() const
{
    QListWidgetItem* item = m_list->currentItem();
    if (!item) return -1;

    bool ok = false;
    const int index = item->data(Qt::UserRole).toInt(&ok);
    if (!ok || index < 0 || index >= m_history->records().size()) return -1;
    return index;
}

void PromptHistoryPage::onSelectionChanged()
{
    rebuildDetails();
    updateActionEnabled();
}

void PromptHistoryPage::updateActionEnabled()
{
    const bool has = selectedRecordIndex() >= 0;
    if (m_requeueBtn) m_requeueBtn->setEnabled(has);
    if (m_saveStateBtn) m_saveStateBtn->setEnabled(has);
    if (m_restoreBtn) m_restoreBtn->setEnabled(has);
}

QWidget* PromptHistoryPage::buildEntryTile(const QString& uuid, const QString& imageFile,
                                           qsizetype tagCount, QWidget* parent)
{
    auto* tile = new QFrame(parent);
    tile->setObjectName(u"PhEntryTile"_s);
    tile->setAttribute(Qt::WA_StyledBackground, true);
    tile->setFixedWidth(kTileWidth + 8); // the 8 covers the frame's padding

    auto* layout = new QVBoxLayout(tile);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* image = new QLabel(tile);
    image->setObjectName(u"PhEntryTileImage"_s);
    image->setFixedSize(kTileWidth, kTileHeight);
    image->setAlignment(Qt::AlignCenter);
    layout->addWidget(image);

    const Entry* entry = m_entries->find(uuid);

    // Keyed on both, so the same image reused across records decodes once.
    const QString key = uuid + u'|' + imageFile;
    const auto cached = m_thumbCache.constFind(key);

    if (cached != m_thumbCache.cend()) {
        image->setPixmap(cached.value());
    } else {
        image->setPixmap(placeholderThumb());
        if (entry) {
            m_pendingThumbs[key].append(QPointer<QLabel>(image));
            // One worker per key; further tiles just join the wait list.
            if (m_pendingThumbs[key].size() == 1)
                requestThumb(key, m_entries->folderFor(uuid) + u"/"_s + imageFile);
        }
    }

    QString titleText;
    if (entry)
        titleText = entry->title.isEmpty() ? u"(untitled)"_s : entry->title;
    else
        titleText = u"uuid %1..."_s.arg(uuid.left(8));

    auto* titleLabel = new QLabel(titleText, tile);
    titleLabel->setObjectName(u"PhEntryTileTitle"_s);
    titleLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    titleLabel->setWordWrap(true);
    // Fixed rather than maximum, so a one-line title takes the same vertical
    // space as a two-line one and the grid rows stay even.
    titleLabel->setFixedSize(kTileWidth, 32);
    layout->addWidget(titleLabel);

    QString tooltip = u"%1\n%2 tag(s)"_s.arg(titleText).arg(tagCount);
    if (entry) {
        // Only a tile whose entry still exists is interactive, rather than
        // looking clickable and doing nothing.
        tile->setCursor(Qt::PointingHandCursor);
        tile->setProperty("_entryUuid", uuid);
        tile->installEventFilter(this);
        tooltip += u"\nClick to open in Entry Viewer"_s;
    }
    tile->setToolTip(tooltip);

    return tile;
}

void PromptHistoryPage::requestThumb(const QString& key, const QString& absolutePath)
{
    auto* watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, key]() {
        watcher->deleteLater();

        const QImage decoded = watcher->result();
        const QPixmap pixmap =
            decoded.isNull() ? placeholderThumb() : QPixmap::fromImage(decoded);
        m_thumbCache.insert(key, pixmap);

        // Guarded pointers: the details pane may have been rebuilt while the
        // decode was running, deleting the labels that asked for it.
        for (const QPointer<QLabel>& label : m_pendingThumbs.take(key))
            if (label) label->setPixmap(pixmap);
    });

    watcher->setFuture(QtConcurrent::run(&decodeThumb, absolutePath));
}

int PromptHistoryPage::computeTileColumns() const
{
    constexpr int kHostMargin = 14;
    constexpr int kSlotWidth = kTileWidth + 8;
    constexpr int kSpacing = 8;

    const int available = m_detailsScroll->viewport()->width() - kHostMargin * 2;
    if (available <= 0) return 1;
    return std::max(1, (available + kSpacing) / (kSlotWidth + kSpacing));
}

void PromptHistoryPage::layoutTileGrid(int columns)
{
    if (!m_tileGridHost) return;

    auto* grid = qobject_cast<QGridLayout*>(m_tileGridHost->layout());
    if (!grid) return;

    // Detached in layout order, which is the order they were added, so the
    // snapshot's own ordering survives the reflow.
    QList<QWidget*> tiles;
    while (grid->count() > 0) {
        QLayoutItem* item = grid->takeAt(0);
        if (item->widget()) tiles.append(item->widget());
        delete item;
    }

    // The previous layout left one trailing column stretched to keep the
    // tiles left-aligned; that has to be cleared before re-placing.
    for (int column = 0; column <= m_tileGridColumns; ++column)
        grid->setColumnStretch(column, 0);

    for (int i = 0; i < int(tiles.size()); ++i)
        grid->addWidget(tiles[i], i / columns, i % columns, Qt::AlignLeft | Qt::AlignTop);

    grid->setColumnStretch(columns, 1);
    m_tileGridColumns = columns;
}

bool PromptHistoryPage::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        auto* widget = qobject_cast<QWidget*>(watched);

        if (mouse->button() == Qt::LeftButton && widget
            && widget->objectName() == "PhEntryTile"_L1
            && widget->rect().contains(mouse->pos())) {
            const QString uuid = widget->property("_entryUuid").toString();
            if (!uuid.isEmpty()) {
                emit openEntryRequested(uuid);
                return true;
            }
        }
    }

    if (event->type() == QEvent::Resize && watched == m_detailsScroll->viewport()
        && m_tileGridHost) {
        const int columns = computeTileColumns();
        if (columns != m_tileGridColumns) layoutTileGrid(columns);
    }

    return QWidget::eventFilter(watched, event);
}

void PromptHistoryPage::rebuildDetails()
{
    while (m_detailsLayout->count()) {
        QLayoutItem* item = m_detailsLayout->takeAt(0);
        if (QWidget* widget = item->widget()) widget->deleteLater();
        delete item;
    }
    m_requeueBtn = nullptr;
    m_saveStateBtn = nullptr;
    m_restoreBtn = nullptr;

    const int index = selectedRecordIndex();
    if (index < 0) {
        auto* empty = dimText(u"Select a record on the left to see details."_s);
        empty->setAlignment(Qt::AlignCenter);
        m_detailsLayout->addWidget(empty);
        m_detailsLayout->addStretch();
        return;
    }

    const PromptRecord& record = m_history->records()[index];

    // ---- Actions
    auto* actionBar = new QFrame;
    actionBar->setObjectName(u"PhActionBar"_s);

    auto* actionLayout = new QHBoxLayout(actionBar);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(6);

    auto makeAction = [](const QString& text, const QString& tooltip) {
        auto* button = new QPushButton(text);
        button->setObjectName(u"PhActionBtn"_s);
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(tooltip);
        return button;
    };

    m_requeueBtn = makeAction(
        u"Re-queue"_s, u"Send the exact same prompt JSON to ComfyUI (same seed)."_s);
    connect(m_requeueBtn, &QPushButton::clicked, this, &PromptHistoryPage::doRequeue);

    m_saveStateBtn = makeAction(u"Save as state"_s,
                                u"Append this snapshot to the composer's saved states."_s);
    connect(m_saveStateBtn, &QPushButton::clicked, this, &PromptHistoryPage::doSaveState);

    m_restoreBtn = makeAction(u"Restore to composer"_s,
                              u"Load this snapshot into the composer and switch pages."_s);
    connect(m_restoreBtn, &QPushButton::clicked, this, &PromptHistoryPage::doRestore);

    actionLayout->addWidget(m_requeueBtn);
    actionLayout->addWidget(m_saveStateBtn);
    actionLayout->addWidget(m_restoreBtn);
    actionLayout->addStretch();
    m_detailsLayout->addWidget(actionBar);

    // ---- Meta
    auto addMetaRow = [this](const QString& key, const QString& value) {
        auto* row = new QFrame;
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);

        auto* keyLabel = new QLabel(key);
        keyLabel->setObjectName(u"PhMetaLabel"_s);
        keyLabel->setFixedWidth(110);

        auto* valueLabel = new QLabel(value);
        valueLabel->setObjectName(u"PhMetaValue"_s);
        valueLabel->setWordWrap(true);

        layout->addWidget(keyLabel);
        layout->addWidget(valueLabel, 1);
        m_detailsLayout->addWidget(row);
    };

    addMetaRow(u"Queued"_s, record.queuedAt.toString(u"yyyy-MM-dd  HH:mm:ss"_s));
    addMetaRow(u"Workflow"_s,
               record.workflowName.isEmpty() ? u"(unnamed)"_s : record.workflowName);

    if (record.batchEntryUuid.isEmpty()) {
        addMetaRow(u"Source"_s, u"composer Run"_s);
    } else {
        const Entry* entry = m_entries->find(record.batchEntryUuid);
        const QString label = (entry && !entry->title.isEmpty())
            ? entry->title
            : u"uuid %1..."_s.arg(record.batchEntryUuid.left(8));
        addMetaRow(u"Source"_s, u"batch  -  %1"_s.arg(label));
    }

    // ---- Prompt
    m_detailsLayout->addWidget(sectionLabel(u"Positive prompt"_s));

    auto* prompt = new QLabel(record.positivePrompt.isEmpty() ? u"(empty)"_s
                                                              : record.positivePrompt);
    prompt->setObjectName(u"PhPromptBlock"_s);
    prompt->setWordWrap(true);
    prompt->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detailsLayout->addWidget(prompt);

    // ---- Workflow variables
    m_detailsLayout->addWidget(sectionLabel(u"Workflow variables"_s));

    auto addKeyValueRow = [this](const QString& key, const QString& value) {
        auto* row = new QFrame;
        row->setObjectName(u"PhVarRow"_s);

        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);

        auto* keyLabel = new QLabel(key);
        keyLabel->setObjectName(u"PhVarKey"_s);
        keyLabel->setFixedWidth(180);

        auto* valueLabel = new QLabel(value);
        valueLabel->setObjectName(u"PhVarValue"_s);
        valueLabel->setWordWrap(true);

        layout->addWidget(keyLabel);
        layout->addWidget(valueLabel, 1);
        m_detailsLayout->addWidget(row);
    };

    if (record.snapshot.workflowVarValues.isEmpty()) {
        m_detailsLayout->addWidget(dimText(u"(no workflow vars)"_s));
    } else {
        for (const QJsonValue entry : record.snapshot.workflowVarValues) {
            const WorkflowVar var = varFromJson(entry.toObject());
            const QString name = var.placeholder.isEmpty()
                ? u"(unnamed %1)"_s.arg(varTypeName(var.value))
                : var.placeholder;
            addKeyValueRow(name, formatVarValue(var));
        }
    }

    // ---- LoRAs
    m_detailsLayout->addWidget(sectionLabel(u"LoRAs"_s));
    if (record.lorasUsed.isEmpty()) {
        m_detailsLayout->addWidget(dimText(u"(no LoRAs)"_s));
    } else {
        for (const Lora& lora : record.lorasUsed) {
            auto* row = new QLabel(formatLora(lora));
            row->setObjectName(u"PhLoraRow"_s);
            row->setWordWrap(true);
            m_detailsLayout->addWidget(row);
        }
    }

    // ---- Rules, the enabled ones with whatever they injected
    m_detailsLayout->addWidget(sectionLabel(u"Rules"_s));
    {
        QStringList lines;
        for (auto it = record.snapshot.ruleStates.cbegin();
             it != record.snapshot.ruleStates.cend(); ++it) {
            if (!it.value()) continue;

            const QStringList arguments = record.snapshot.ruleArguments.value(it.key());
            lines << (arguments.isEmpty()
                          ? it.key()
                          : u"%1   [%2]"_s.arg(it.key(), arguments.join(u", "_s)));
        }

        if (lines.isEmpty()) {
            m_detailsLayout->addWidget(dimText(u"(no enabled rules)"_s));
        } else {
            for (const QString& line : lines) {
                auto* row = new QLabel(line);
                row->setObjectName(u"PhVarRow"_s);
                row->setWordWrap(true);
                m_detailsLayout->addWidget(row);
            }
        }
    }

    // ---- Replacement variables
    m_detailsLayout->addWidget(sectionLabel(u"Replacement variables"_s));
    if (record.snapshot.varValues.isEmpty()) {
        m_detailsLayout->addWidget(dimText(u"(no variables)"_s));
    } else {
        for (const auto& [name, value] : record.snapshot.varValues)
            addKeyValueRow(u"$%1$"_s.arg(name), value.isEmpty() ? u"(empty)"_s : value);
    }

    // ---- Active entries
    m_detailsLayout->addWidget(sectionLabel(u"Active entries"_s));
    m_tileGridHost.clear();
    m_tileGridColumns = 0;

    if (record.snapshot.activePushes.isEmpty()) {
        m_detailsLayout->addWidget(dimText(u"(no entries pushed)"_s));
    } else {
        auto* tileHost = new QWidget(m_detailsHost);
        auto* grid = new QGridLayout(tileHost);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(8);

        for (const EntryPush& push : record.snapshot.activePushes)
            grid->addWidget(buildEntryTile(push.entryUuid, push.imageFile, push.tags.size(),
                                           tileHost));

        m_detailsLayout->addWidget(tileHost);
        m_tileGridHost = tileHost;
        layoutTileGrid(computeTileColumns());
    }

    m_detailsLayout->addStretch();
    updateActionEnabled();
}

void PromptHistoryPage::doRequeue()
{
    const int index = selectedRecordIndex();
    if (index < 0) return;

    const PromptRecord& original = m_history->records()[index];
    if (original.renderedJson.isEmpty()) {
        emit statusMessage(u"Re-queue: empty JSON."_s);
        return;
    }

    // A replay gets its own row, so the history reflects every actual push.
    // Only the timestamp changes.
    PromptRecord replay = original;
    replay.queuedAt = QDateTime::currentDateTime();

    // Copied out before the append: prepending can reallocate the list and
    // leave `original` dangling.
    QJsonObject bakedState;
    bakedState[u"tagcomposer_state"_s] = replay.snapshot.toJson();
    const QString renderedJson = replay.renderedJson;

    m_history->append(std::move(replay));
    m_comfy->queue(renderedJson, bakedState);
    emit statusMessage(u"Re-queued prompt with same seed."_s);
}

void PromptHistoryPage::doSaveState()
{
    const int index = selectedRecordIndex();
    if (index < 0) return;

    const PromptRecord& record = m_history->records()[index];
    const QString name = u"History %1 (%2)"_s.arg(
        record.queuedAt.toString(u"HH:mm:ss"_s),
        record.workflowName.isEmpty() ? u"?"_s : record.workflowName);

    m_composer->appendSnapshotAsState(record.snapshot, name);
    emit statusMessage(u"Saved snapshot as: %1"_s.arg(name));
}

void PromptHistoryPage::doRestore()
{
    const int index = selectedRecordIndex();
    if (index < 0) return;

    m_composer->restoreFromSnapshot(m_history->records()[index].snapshot);
    emit statusMessage(u"Restored snapshot to composer."_s);
    emit switchToComposerRequested();
}

} // namespace tc
