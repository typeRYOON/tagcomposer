#include <gui/prompthistorypage.h>
#include <gui/composer/promptcomposerpage.h>
#include <gui/widgets/appscrollbar.h>
#include <core/prompthistory.h>
#include <core/entrymodel.h>
#include <core/comfyuiclient.h>
#include <core/workflowmanager.h>
#include <utils/appconfig.h>
#include <QEvent>
#include <QFrame>
#include <QFuture>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

using namespace core;

namespace gui {

namespace {

// "WfType: value" short string for the var-list rows. Mirrors WorkflowVar
// fields so users see a faithful representation of what was sent.
QString formatVarValue(const WorkflowVar& var)
{
    switch (var.type) {
    case WorkflowVarType::Seed:
        return QString::number(var.seedValue);
    case WorkflowVarType::String:
        return var.stringValue.isEmpty() ? QStringLiteral("(empty)") : var.stringValue;
    case WorkflowVarType::Integer:
        return QString::number(var.intValue);
    case WorkflowVarType::Float:
        return QString::number(var.floatValue, 'f', 4);
    case WorkflowVarType::DirSearch:
        return var.selectedFile.isEmpty() ? QStringLiteral("(none)") : var.selectedFile;
    case WorkflowVarType::LatentSize:
        return QString("%1x%2").arg(var.latentWidth).arg(var.latentHeight);
    case WorkflowVarType::Image:
        return var.imageUuid.isEmpty() ? QStringLiteral("(none)") : var.imageUuid.left(8) + "...";
    case WorkflowVarType::Wildcard:
        return QString("[%1 wildcard line(s)]").arg(var.wildcardTags.size());
    }
    return QStringLiteral("?");
}

QString formatLora(const LoraConfig& lc)
{
    const QString name = lc.file.isEmpty() ? QStringLiteral("(empty)") : lc.file;
    return QString("%1  -  model %2 / clip %3")
        .arg(name)
        .arg(lc.modelStr, 0, 'f', 2)
        .arg(lc.clipStr, 0, 'f', 2);
}

QLabel* makeSectionLabel(const QString& text)
{
    auto* lbl = new QLabel(text);
    lbl->setObjectName("PhSectionLabel");
    return lbl;
}

constexpr int kTileW = 120;
constexpr int kTileH = 150;

// Worker-side decode + scale + center-crop. Pure function so it can run on
// any thread without touching Qt's GUI state.
QImage decodeThumb(const QString& path)
{
    QImage src(path);
    if (src.isNull()) return QImage();
    QImage scaled =
        src.scaled(kTileW, kTileH, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int x = std::max(0, (scaled.width() - kTileW) / 2);
    const int y = std::max(0, (scaled.height() - kTileH) / 2);
    return scaled.copy(x, y, kTileW, kTileH);
}

QPixmap placeholderThumb()
{
    QPixmap pm(kTileW, kTileH);
    pm.fill(QColor("#141414"));
    return pm;
}

} // namespace

QWidget* PromptHistoryPage::buildEntryTile(const QString& uuid, const QString& imageFileName,
                                           int tagCount, QWidget* parent)
{
    auto* tile = new QFrame(parent);
    tile->setObjectName("PhEntryTile");
    tile->setAttribute(Qt::WA_StyledBackground, true);
    tile->setFixedWidth(kTileW + 8); // +8 covers padding in the QSS rule

    auto* lay = new QVBoxLayout(tile);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);

    auto* img = new QLabel(tile);
    img->setObjectName("PhEntryTileImage");
    img->setFixedSize(kTileW, kTileH);
    img->setAlignment(Qt::AlignCenter);
    lay->addWidget(img);

    Entry* e = m_entryModel ? m_entryModel->entryByUuid(uuid) : nullptr;

    // Cache key by uuid + filename so the same image across records reuses
    // the same decoded pixmap.
    const QString key = uuid + QLatin1Char('|') + imageFileName;
    auto cacheIt = m_thumbCache.constFind(key);
    if (cacheIt != m_thumbCache.constEnd()) {
        img->setPixmap(cacheIt.value());
    }
    else {
        img->setPixmap(placeholderThumb());
        if (e) {
            const QString path =
                utils::BASE_PATH + "/data/entry/" + e->uuid + "/" + imageFileName;
            m_pendingThumbs[key].append(QPointer<QLabel>(img));
            // Only one worker per key - additional tiles join the same wait list.
            if (m_pendingThumbs[key].size() == 1) requestThumb(key, path);
        }
    }

    QString titleText;
    if (e && !e->title.isEmpty())
        titleText = e->title;
    else if (e)
        titleText = QStringLiteral("(untitled)");
    else
        titleText = QString("uuid %1...").arg(uuid.left(8));

    auto* titleLbl = new QLabel(titleText, tile);
    titleLbl->setObjectName("PhEntryTileTitle");
    titleLbl->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    titleLbl->setWordWrap(true);
    // Fixed (not maximum) so a one-line title leaves the same vertical
    // footprint as a two-line one; longer titles just clip past line two.
    titleLbl->setFixedSize(kTileW, 32);
    lay->addWidget(titleLbl);

    QString tip = QString("%1\n%2 tag(s)").arg(titleText).arg(tagCount);
    if (e) {
        // Clickable: jump to entry viewer. Skipped for missing entries so we
        // don't pretend the row is interactive when it isn't.
        tile->setCursor(Qt::PointingHandCursor);
        tile->setProperty("_entryId", int(e->id));
        tile->installEventFilter(this);
        tip += QStringLiteral("\nClick to open in Entry Viewer");
    }
    tile->setToolTip(tip);
    return tile;
}

bool PromptHistoryPage::eventFilter(QObject* obj, QEvent* ev)
{
    if (ev->type() == QEvent::MouseButtonRelease) {
        auto* mev = static_cast<QMouseEvent*>(ev);
        if (mev->button() == Qt::LeftButton) {
            if (auto* w = qobject_cast<QWidget*>(obj)) {
                if (w->objectName() == QLatin1String("PhEntryTile") && w->rect().contains(mev->pos())) {
                    bool ok = false;
                    const int id = w->property("_entryId").toInt(&ok);
                    if (ok && id >= 0) {
                        emit openEntryRequested(id);
                        return true;
                    }
                }
            }
        }
    }
    if (ev->type() == QEvent::Resize && m_detailsScroll && obj == m_detailsScroll->viewport()) {
        if (m_tileGridHost) {
            const int cols = computeTileCols();
            if (cols != m_tileGridCols) layoutTileGrid(cols);
        }
    }
    return QWidget::eventFilter(obj, ev);
}

int PromptHistoryPage::computeTileCols() const
{
    // m_detailsLayout has 14/10 horizontal margins; the tile occupies
    // (kTileW + 8) -- 8 covers the QFrame padding from the QSS. Spacing
    // between tiles is 8.
    constexpr int kHostHMargin = 14;
    constexpr int kSlotW = kTileW + 8;
    constexpr int kSpacing = 8;
    const int avail =
        m_detailsScroll ? (m_detailsScroll->viewport()->width() - kHostHMargin * 2) : 0;
    if (avail <= 0) return 1;
    return std::max(1, (avail + kSpacing) / (kSlotW + kSpacing));
}

void PromptHistoryPage::layoutTileGrid(int cols)
{
    if (!m_tileGridHost) return;
    auto* grid = qobject_cast<QGridLayout*>(m_tileGridHost->layout());
    if (!grid) return;

    // Detach tiles in their current order, then re-place into the new column
    // count. takeAt returns items in layout order which mirrors insertion
    // order (row-major), so iteration preserves the user's snapshot order.
    QList<QWidget*> tiles;
    while (grid->count() > 0) {
        QLayoutItem* item = grid->takeAt(0);
        if (item->widget()) tiles.append(item->widget());
        delete item;
    }
    // Clear any column stretches left from the previous layout (one extra
    // column got stretch=1 to keep tiles left-aligned).
    for (int c = 0; c <= m_tileGridCols; ++c) grid->setColumnStretch(c, 0);

    for (int i = 0; i < tiles.size(); ++i)
        grid->addWidget(tiles[i], i / cols, i % cols, Qt::AlignLeft | Qt::AlignTop);
    grid->setColumnStretch(cols, 1);
    m_tileGridCols = cols;
}

void PromptHistoryPage::requestThumb(const QString& key, const QString& absPath)
{
    auto* watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, key]() {
        const QImage result = watcher->result();
        const QPixmap pm = result.isNull() ? placeholderThumb() : QPixmap::fromImage(result);
        m_thumbCache.insert(key, pm);
        const auto pending = m_pendingThumbs.take(key);
        for (const QPointer<QLabel>& lbl : pending)
            if (lbl) lbl->setPixmap(pm);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(&decodeThumb, absPath));
}

PromptHistoryPage::PromptHistoryPage(PromptHistory* history, EntryModel* entryModel,
                                     PromptComposerPage* composer, ComfyUiClient* comfy,
                                     QWidget* parent)
    : QWidget(parent), m_history(history), m_entryModel(entryModel), m_composer(composer),
      m_comfy(comfy)
{
    setObjectName("PromptHistoryPage");
    setAttribute(Qt::WA_StyledBackground, true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- Header bar (mirrors WfEditHeader / OvHeader: 50px tall, dark bg
    // with a bottom rule; title/subtitle reuse the workflow-editor styles).
    auto* header = new QWidget(this);
    header->setObjectName("PhHeader");
    header->setAttribute(Qt::WA_StyledBackground, true);
    header->setFixedHeight(50);

    auto* hdrLay = new QHBoxLayout(header);
    hdrLay->setContentsMargins(20, 12, 20, 12);
    hdrLay->setSpacing(16);

    auto* title = new QLabel("PROMPT HISTORY", header);
    title->setObjectName("WfEditTitle");

    auto* subtitle = new QLabel("session only - cleared on app close", header);
    subtitle->setObjectName("WfEditSubtitle");

    m_clearAllBtn = new QPushButton("Clear", header);
    m_clearAllBtn->setObjectName("PhClearBtn");
    m_clearAllBtn->setCursor(Qt::PointingHandCursor);

    hdrLay->addWidget(title);
    hdrLay->addWidget(subtitle, 1);
    hdrLay->addWidget(m_clearAllBtn);
    root->addWidget(header);

    // ---- Split: list (left) / details (right)
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName("PhSplit");
    m_splitter->setHandleWidth(2);
    m_splitter->setChildrenCollapsible(false);

    m_listWidget = new QListWidget(this);
    m_listWidget->setObjectName("PhList");
    m_listWidget->setVerticalScrollBar(new AppScrollBar(Qt::Vertical));
    m_listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_listWidget->setFocusPolicy(Qt::NoFocus);

    m_detailsScroll = new QScrollArea(this);
    m_detailsScroll->setObjectName("PhDetailsScroll");
    m_detailsScroll->setWidgetResizable(true);
    m_detailsScroll->setFrameShape(QFrame::NoFrame);
    m_detailsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_detailsScroll->setVerticalScrollBar(new AppScrollBar(Qt::Vertical));
    // Width-responsive tile grid: catch viewport resizes so splitter drags
    // and page resizes both retrigger the column-count computation.
    m_detailsScroll->viewport()->installEventFilter(this);

    m_detailsHost = new QWidget(m_detailsScroll);
    m_detailsHost->setObjectName("PhDetailsHost");
    m_detailsLayout = new QVBoxLayout(m_detailsHost);
    m_detailsLayout->setContentsMargins(14, 10, 14, 10);
    m_detailsLayout->setSpacing(6);
    m_detailsScroll->setWidget(m_detailsHost);

    m_splitter->addWidget(m_listWidget);
    m_splitter->addWidget(m_detailsScroll);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({280, 800});
    root->addWidget(m_splitter, 1);

    // Empty placeholder takes the splitter's slot when there are no records.
    m_emptyLabel = new QLabel("No prompts queued this session.", this);
    m_emptyLabel->setObjectName("PhEmpty");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(m_emptyLabel, 1);
    m_emptyLabel->hide();

    rebuildList();

    // ---- Wire history -> list
    connect(m_history, &PromptHistory::recordAdded, this, [this](int) {
        // Prepend semantics: keep the user's current selection pinned to the
        // same record by reading it before the rebuild.
        const int prevIdx = selectedRecordIndex();
        rebuildList();
        if (prevIdx < 0) return;
        // Old index shifts by +1 after a prepend (the new record sits at 0).
        const int newRow = prevIdx + 1;
        if (newRow >= 0 && newRow < m_listWidget->count())
            m_listWidget->setCurrentRow(newRow);
    });
    connect(m_history, &PromptHistory::cleared, this, [this]() { rebuildList(); });

    connect(m_listWidget, &QListWidget::currentRowChanged, this,
            [this](int) { onSelectionChanged(); });
    connect(m_clearAllBtn, &QPushButton::clicked, this, [this]() {
        if (m_history) m_history->clear();
    });

    onSelectionChanged();
}

void PromptHistoryPage::rebuildList()
{
    m_listWidget->blockSignals(true);
    m_listWidget->clear();

    const auto& recs = m_history->records();
    const bool empty = recs.isEmpty();
    m_splitter->setVisible(!empty);
    m_emptyLabel->setVisible(empty);

    for (int i = 0; i < recs.size(); ++i) {
        const PromptRecord& r = recs[i];
        const QString time = r.queuedAt.toString("HH:mm:ss");
        const QString name = r.workflowName.isEmpty() ? QStringLiteral("(unnamed)") : r.workflowName;
        const int varCount = r.snapshot.workflowVarValues.size();
        const int loraCount = r.lorasUsed.size();
        const int entryCount = r.snapshot.activePushes.size();
        QString label =
            QString("%1   %2\n%3 var(s)  -  %4 LoRA(s)  -  %5 entr%6")
                .arg(time, name)
                .arg(varCount)
                .arg(loraCount)
                .arg(entryCount)
                .arg(entryCount == 1 ? "y" : "ies");
        if (r.batchEntryId >= 0) label += "  -  batch";
        auto* item = new QListWidgetItem(label, m_listWidget);
        item->setData(Qt::UserRole, i);
    }
    if (!recs.isEmpty()) m_listWidget->setCurrentRow(0);
    m_listWidget->blockSignals(false);
    onSelectionChanged();
}

int PromptHistoryPage::selectedRecordIndex() const
{
    auto* item = m_listWidget->currentItem();
    if (!item) return -1;
    bool ok = false;
    const int idx = item->data(Qt::UserRole).toInt(&ok);
    if (!ok) return -1;
    if (idx < 0 || idx >= m_history->records().size()) return -1;
    return idx;
}

void PromptHistoryPage::onSelectionChanged()
{
    rebuildDetails();
    updateActionEnabled();
}

void PromptHistoryPage::updateActionEnabled()
{
    const bool ok = selectedRecordIndex() >= 0;
    if (m_requeueBtn) m_requeueBtn->setEnabled(ok && m_comfy);
    if (m_saveStateBtn) m_saveStateBtn->setEnabled(ok && m_composer);
    if (m_restoreBtn) m_restoreBtn->setEnabled(ok && m_composer);
}

void PromptHistoryPage::rebuildDetails()
{
    // Tear down existing children.
    while (m_detailsLayout->count()) {
        auto* item = m_detailsLayout->takeAt(0);
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }
    m_requeueBtn = m_saveStateBtn = m_restoreBtn = nullptr;

    const int idx = selectedRecordIndex();
    if (idx < 0) {
        auto* empty = new QLabel("Select a record on the left to see details.");
        empty->setObjectName("PhDimText");
        empty->setAlignment(Qt::AlignCenter);
        m_detailsLayout->addWidget(empty);
        m_detailsLayout->addStretch();
        return;
    }
    const PromptRecord& r = m_history->records()[idx];

    // ---- Action bar (top)
    auto* actionBar = new QFrame();
    actionBar->setObjectName("PhActionBar");
    auto* abLay = new QHBoxLayout(actionBar);
    abLay->setContentsMargins(0, 0, 0, 0);
    abLay->setSpacing(6);

    m_requeueBtn = new QPushButton("Re-queue");
    m_requeueBtn->setObjectName("PhActionBtn");
    m_requeueBtn->setCursor(Qt::PointingHandCursor);
    m_requeueBtn->setToolTip("Send the exact same prompt JSON to ComfyUI (same seed).");
    connect(m_requeueBtn, &QPushButton::clicked, this, &PromptHistoryPage::doRequeue);

    m_saveStateBtn = new QPushButton("Save as state");
    m_saveStateBtn->setObjectName("PhActionBtn");
    m_saveStateBtn->setCursor(Qt::PointingHandCursor);
    m_saveStateBtn->setToolTip("Append this snapshot to the composer's saved states.");
    connect(m_saveStateBtn, &QPushButton::clicked, this, &PromptHistoryPage::doSaveState);

    m_restoreBtn = new QPushButton("Restore to composer");
    m_restoreBtn->setObjectName("PhActionBtn");
    m_restoreBtn->setCursor(Qt::PointingHandCursor);
    m_restoreBtn->setToolTip("Load this snapshot into the composer and switch pages.");
    connect(m_restoreBtn, &QPushButton::clicked, this, &PromptHistoryPage::doRestore);

    abLay->addWidget(m_requeueBtn);
    abLay->addWidget(m_saveStateBtn);
    abLay->addWidget(m_restoreBtn);
    abLay->addStretch();
    m_detailsLayout->addWidget(actionBar);

    // ---- Meta header
    auto addMetaRow = [&](const QString& key, const QString& value) {
        auto* row = new QFrame();
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(8);
        auto* k = new QLabel(key);
        k->setObjectName("PhMetaLabel");
        k->setFixedWidth(110);
        auto* v = new QLabel(value);
        v->setObjectName("PhMetaValue");
        v->setWordWrap(true);
        rl->addWidget(k);
        rl->addWidget(v, 1);
        m_detailsLayout->addWidget(row);
    };

    addMetaRow("Queued",
               r.queuedAt.toString("yyyy-MM-dd  HH:mm:ss"));
    addMetaRow("Workflow", r.workflowName.isEmpty() ? QStringLiteral("(unnamed)") : r.workflowName);
    if (r.batchEntryId >= 0) {
        QString entryLabel = QString("entry id %1").arg(r.batchEntryId);
        if (m_entryModel) {
            if (Entry* e = m_entryModel->entryById(r.batchEntryId)) {
                if (!e->title.isEmpty()) entryLabel = e->title;
            }
        }
        addMetaRow("Source", QString("batch  -  %1").arg(entryLabel));
    }
    else {
        addMetaRow("Source", "composer Run");
    }

    // ---- Positive prompt
    m_detailsLayout->addWidget(makeSectionLabel("Positive prompt"));
    auto* prompt = new QLabel(r.positivePrompt.isEmpty() ? QStringLiteral("(empty)")
                                                         : r.positivePrompt);
    prompt->setObjectName("PhPromptBlock");
    prompt->setWordWrap(true);
    prompt->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detailsLayout->addWidget(prompt);

    // ---- Workflow vars
    m_detailsLayout->addWidget(makeSectionLabel("Workflow variables"));
    if (r.snapshot.workflowVarValues.isEmpty()) {
        auto* none = new QLabel("(no workflow vars)");
        none->setObjectName("PhDimText");
        m_detailsLayout->addWidget(none);
    }
    else {
        for (const QJsonValue& entry : r.snapshot.workflowVarValues) {
            WorkflowVar v = WorkflowManager::varFromJson(entry.toObject());
            auto* row = new QFrame();
            row->setObjectName("PhVarRow");
            auto* rl = new QHBoxLayout(row);
            rl->setContentsMargins(0, 0, 0, 0);
            rl->setSpacing(8);

            const QString ph = v.placeholder.isEmpty()
                                   ? QString("(unnamed %1)").arg(WorkflowManager::typeToStr(v.type))
                                   : v.placeholder;
            auto* k = new QLabel(ph);
            k->setObjectName("PhVarKey");
            k->setFixedWidth(180);
            auto* val = new QLabel(formatVarValue(v));
            val->setObjectName("PhVarValue");
            val->setWordWrap(true);
            rl->addWidget(k);
            rl->addWidget(val, 1);
            m_detailsLayout->addWidget(row);
        }
    }

    // ---- LoRAs
    m_detailsLayout->addWidget(makeSectionLabel("LoRAs"));
    if (r.lorasUsed.isEmpty()) {
        auto* none = new QLabel("(no LoRAs)");
        none->setObjectName("PhDimText");
        m_detailsLayout->addWidget(none);
    }
    else {
        for (const LoraConfig& lc : r.lorasUsed) {
            auto* row = new QLabel(formatLora(lc));
            row->setObjectName("PhLoraRow");
            row->setWordWrap(true);
            m_detailsLayout->addWidget(row);
        }
    }

    // ---- Rules (enabled only, with their Add/Replace args when present).
    m_detailsLayout->addWidget(makeSectionLabel("Rules"));
    {
        QStringList enabledLines;
        for (auto it = r.snapshot.ruleStates.cbegin(); it != r.snapshot.ruleStates.cend(); ++it) {
            if (!it.value()) continue;
            const QList<QString> args = r.snapshot.ruleArguments.value(it.key());
            if (args.isEmpty())
                enabledLines << it.key();
            else
                enabledLines << QString("%1   [%2]").arg(it.key(), args.join(", "));
        }
        if (enabledLines.isEmpty()) {
            auto* none = new QLabel("(no enabled rules)");
            none->setObjectName("PhDimText");
            m_detailsLayout->addWidget(none);
        }
        else {
            for (const QString& line : enabledLines) {
                auto* row = new QLabel(line);
                row->setObjectName("PhVarRow");
                row->setWordWrap(true);
                m_detailsLayout->addWidget(row);
            }
        }
    }

    // ---- Replacement variables ($name$ -> value).
    m_detailsLayout->addWidget(makeSectionLabel("Replacement variables"));
    if (r.snapshot.varValues.isEmpty()) {
        auto* none = new QLabel("(no variables)");
        none->setObjectName("PhDimText");
        m_detailsLayout->addWidget(none);
    }
    else {
        for (const auto& pair : r.snapshot.varValues) {
            auto* row = new QFrame();
            row->setObjectName("PhVarRow");
            auto* rl = new QHBoxLayout(row);
            rl->setContentsMargins(0, 0, 0, 0);
            rl->setSpacing(8);
            auto* k = new QLabel(QString("$%1$").arg(pair.first));
            k->setObjectName("PhVarKey");
            k->setFixedWidth(180);
            const QString shown = pair.second.isEmpty() ? QStringLiteral("(empty)") : pair.second;
            auto* val = new QLabel(shown);
            val->setObjectName("PhVarValue");
            val->setWordWrap(true);
            rl->addWidget(k);
            rl->addWidget(val, 1);
            m_detailsLayout->addWidget(row);
        }
    }

    // ---- Active entries (thumbnail tiles in a width-responsive grid).
    m_detailsLayout->addWidget(makeSectionLabel("Active entries"));
    m_tileGridHost.clear();
    m_tileGridCols = 0;
    if (r.snapshot.activePushes.isEmpty()) {
        auto* none = new QLabel("(no entries pushed)");
        none->setObjectName("PhDimText");
        m_detailsLayout->addWidget(none);
    }
    else {
        auto* tileHost = new QWidget(m_detailsHost);
        auto* grid = new QGridLayout(tileHost);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(8);
        for (int i = 0; i < r.snapshot.activePushes.size(); ++i) {
            const EntryPush& p = r.snapshot.activePushes[i];
            QWidget* tile = buildEntryTile(p.uuid, p.imageFileName, p.tags.size(), tileHost);
            grid->addWidget(tile); // position filled in by layoutTileGrid below
        }
        m_detailsLayout->addWidget(tileHost);
        m_tileGridHost = tileHost;
        layoutTileGrid(computeTileCols());
    }

    m_detailsLayout->addStretch();
    updateActionEnabled();
}

void PromptHistoryPage::doRequeue()
{
    const int idx = selectedRecordIndex();
    if (idx < 0 || !m_comfy) return;
    const PromptRecord& orig = m_history->records()[idx];
    if (orig.renderedJson.isEmpty()) {
        emit statusMessageRequested("Re-queue: empty JSON.");
        return;
    }
    // Replay records appear as their own rows so the history reflects every
    // actual push. Snapshot/loras/prompt are copied from the original; only
    // the timestamp is refreshed.
    PromptRecord replay = orig;
    replay.queuedAt = QDateTime::currentDateTime();
    // Bake the original snapshot into the replayed output too. Copies taken
    // before append - the prepend can reallocate and dangle `orig`.
    QJsonObject pngInfo;
    pngInfo["tagcomposer_state"] = replay.snapshot.toJson();
    const QString renderedJson = replay.renderedJson;
    m_history->append(std::move(replay));
    m_comfy->queuePrompt(renderedJson, pngInfo);
    emit statusMessageRequested("Re-queued prompt with same seed.");
}

void PromptHistoryPage::doSaveState()
{
    const int idx = selectedRecordIndex();
    if (idx < 0 || !m_composer) return;
    const PromptRecord& r = m_history->records()[idx];
    const QString name = QString("History %1 (%2)")
                             .arg(r.queuedAt.toString("HH:mm:ss"))
                             .arg(r.workflowName.isEmpty() ? QStringLiteral("?") : r.workflowName);
    m_composer->appendSnapshotAsState(r.snapshot, name);
    emit statusMessageRequested(QString("Saved snapshot as: %1").arg(name));
}

void PromptHistoryPage::doRestore()
{
    const int idx = selectedRecordIndex();
    if (idx < 0 || !m_composer) return;
    const PromptRecord& r = m_history->records()[idx];
    m_composer->restoreFromSnapshot(r.snapshot);
    emit statusMessageRequested("Restored snapshot to composer.");
    emit switchToComposerRequested();
}

} // namespace gui
