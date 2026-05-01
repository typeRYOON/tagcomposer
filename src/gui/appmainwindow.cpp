#include <gui/appmainwindow.h>
#include <gui/widgets/navbar.h>
#include <gui/homepage.h>
#include <gui/tileview/tileviewpage.h>
#include <gui/widgets/statusbar.h>
#include <gui/composer/promptcomposerpage.h>
#include <gui/faceteditorpage.h>
#include <gui/tagwikipage.h>
#include <gui/settingspage.h>
#include <gui/workfloweditpage.h>
#include <gui/datasethelperspage.h>
#include <gui/widgets/danmakuoverlay.h>
#include <gui/exportdialog.h>
#include <gui/importdialog.h>
#include <utils/qutils.h>
#include <utils/appconfig.h>
#include <QApplication>
#include <QEvent>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QVBoxLayout>
#include <QTimer>
#include <QFutureWatcher>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QShortcut>
#include <QtConcurrent>

using namespace utils;

namespace gui {

AppMainWindow::AppMainWindow(QWidget* parent)
    : QMainWindow{ parent },
    m_entryModel{ new core::EntryModel(this) }
{
    setWindowTitle("TagComposer");
    setWindowOpacity(0.0);

    // ── Load settings ─────────────────────────────────────────────────────────
    m_settings = AppSettings::load(BASE_PATH + "/" + SETTINGS_PATH);

    // ── ComfyUI client ────────────────────────────────────────────────────────
    m_comfyClient = new core::ComfyUiClient(this);
    m_comfyClient->setServerAddress(m_settings.comfyUiServerAddress);
    m_comfyClient->setApiKey(m_settings.comfyUiApiKey);

    // ── Load pipeline data ────────────────────────────────────────────────────
    m_facetIndex       = core::FacetIndex::loadFromFile(BASE_PATH + "/" + FACETS_PATH);
    m_facetIndex.loadDefinitionsFromFile(BASE_PATH + "/" + DEFINITIONS_PATH);
    m_ruleEngine       = core::RuleEngine::loadFromFile(BASE_PATH + "/" + RULES_PATH);
    m_tagGroupIndex    = core::TagGroupIndex::loadFromFile(BASE_PATH + "/" + GROUPS_PATH);
    m_varIndex         = core::VariableIndex::loadFromFile(BASE_PATH + "/" + VARS_PATH);
    m_workflowManager  = core::WorkflowManager::loadFromFile(BASE_PATH + "/" + WORKFLOWS_PATH);
    m_pipeline         = new core::PromptPipeline(&m_facetIndex, &m_ruleEngine, &m_varIndex, this);

    // ── Pages ─────────────────────────────────────────────────────────────────
    m_tileViewPage    = new TileViewPage(m_entryModel, this);
    m_tileViewPage->setLoraBaseDir(m_settings.loraBaseDir);
    m_composerPage    = new PromptComposerPage(m_pipeline, &m_ruleEngine, m_tagGroupIndex, this);
    m_composerPage->setVariableIndex(&m_varIndex);
    m_composerPage->setWorkflowManager(&m_workflowManager, BASE_PATH + "/" + WORKFLOWS_PATH);
    m_composerPage->setStatesDir(BASE_PATH + "/" + STATES_DIR);
    m_composerPage->setEntryModel(m_entryModel);
    m_composerPage->setOutputFolderPattern(m_settings.comfyUiOutputFolder);
    m_composerPage->setTempFolder(m_settings.comfyUiTempFolder);
    m_composerPage->setQuickFacets(m_settings.quickCharacterFacet, m_settings.quickCopyrightFacet);
    m_facetEditorPage = new FacetEditorPage(&m_facetIndex, m_entryModel, this);
    m_facetEditorPage->setActiveTagsProvider(
        [this]() { return m_composerPage->currentActiveTags(); });

    m_wikiPage          = new TagWikiPage(this);
    m_settingsPage      = new SettingsPage(&m_settings, this);
    m_workflowEditPage  = new WorkflowEditPage(this);
    m_workflowEditPage->setWorkflowManager(&m_workflowManager, BASE_PATH + "/" + WORKFLOWS_PATH);
    m_workflowEditPage->setEntryModel(m_entryModel);

    m_pages = new QStackedWidget(this);
    m_pages->setObjectName("MainPages");
    m_pages->installEventFilter(this);
    m_pages->addWidget(new HomePage(this));          // 0
    m_pages->addWidget(m_tileViewPage);              // 1
    m_pages->addWidget(m_composerPage);              // 2
    m_pages->addWidget(m_facetEditorPage);           // 3
    m_pages->addWidget(m_wikiPage);                  // 4
    m_pages->addWidget(m_workflowEditPage);          // 5
    m_pages->addWidget(new DatasetHelpersPage(this)); // 6
    m_pages->addWidget(m_settingsPage);              // 7

    // ── Danmaku overlay (behind all pages) ────────────────────────────────────
    m_danmakuOverlay = new DanmakuOverlay(m_pages);

    NavBar* nav = new NavBar(this, this);
    connect(nav,     &NavBar::pageRequested,        m_pages, &QStackedWidget::setCurrentIndex);
    connect(m_pages, &QStackedWidget::currentChanged, nav,   &NavBar::setCurrentPage);

    // ── LoRA stack: tile view → main window + composer + workflow editor ─────
    connect(m_tileViewPage, &TileViewPage::loraStackChanged,
            this, [this](const QList<core::LoraConfig>& stack) {
                m_activeLoraStack = stack;
                m_activeLoraUuids = m_tileViewPage->activeLoraUuids();
                m_composerPage->setActiveLoraUuids(m_activeLoraUuids);
                m_workflowEditPage->setActiveLoraStack(stack);
            });

    // Strength edits in the workflow editor mutate the entries directly;
    // mirror the new values into our cached stack so the next workflow run
    // uses them without waiting for a tile-view re-emit.
    connect(m_workflowEditPage, &WorkflowEditPage::loraStrengthsChanged,
            this, [this](const QList<core::LoraConfig>& stack) {
                m_activeLoraStack = stack;
            });

    // ── Batch ─────────────────────────────────────────────────────────────────
    connect(m_workflowEditPage, &WorkflowEditPage::batchRunRequested,
            this, &AppMainWindow::runBatch);

    // ── LoRA restore: composer session/state → tile view ─────────────────────
    connect(m_composerPage, &PromptComposerPage::loraUuidsRestored,
            m_tileViewPage, &TileViewPage::setLoraActiveByUuids);

    // ── Wire export: extraBtn → composer page ─────────────────────────────────
    connect(m_tileViewPage, &TileViewPage::tagsExported,
            m_composerPage, &PromptComposerPage::loadPipeline);
    connect(m_tileViewPage, &TileViewPage::entryTagAdded,
            m_composerPage, &PromptComposerPage::onEntryTagAdded);
    connect(m_tileViewPage, &TileViewPage::entryTagRemoved,
            m_composerPage, &PromptComposerPage::onEntryTagRemoved);

    // ── Sync push-group state back to tile view ────────────────────────────────
    connect(m_composerPage, &PromptComposerPage::activeGroupsChanged,
            m_tileViewPage, &TileViewPage::setActiveGroups);

    // ── Wire facet editor reload ───────────────────────────────────────────────
    connect(m_facetEditorPage, &FacetEditorPage::facetsDefined,
            this, &AppMainWindow::reloadFacets);

    // Schema reload — re-read facets.fct, keep tag definitions intact
    connect(m_facetEditorPage, &FacetEditorPage::schemaReloadRequested,
            this, [this]() {
                m_facetIndex.reloadSchemaFromFile(BASE_PATH + "/" + FACETS_PATH);
                reloadFacets();
                m_statusBar->showMessage("Facet schema reloaded.");
            });

    // ── Composer quick-add facet shortcut ─────────────────────────────────────
    // Right-click → "Quick add as character/copyright" mutates the FacetIndex
    // here (composer doesn't own the index), persists, and reloads.
    connect(m_composerPage, &PromptComposerPage::quickFacetRequested,
            this, [this](const QString& tag, const QString& facetName) {
                if (tag.isEmpty() || facetName.isEmpty()) return;
                QList<QString> existing = m_facetIndex.facetsFor(tag);
                if (existing.contains(facetName)) {
                    m_statusBar->showMessage(
                        QString("'%1' already has facet '%2'").arg(tag, facetName));
                    return;
                }
                existing << facetName;
                m_facetIndex.setDefinition(tag, existing);
                reloadFacets();  // also persists via saveDefinitions
                m_statusBar->showMessage(
                    QString("Added facet '%1' to '%2'").arg(facetName, tag));
            });

    // ── Wiki page navigation ───────────────────────────────────────────────────
    auto showWiki = [this](const QString& tag) {
        m_wikiPage->lookupTag(tag);
        m_pages->setCurrentIndex(4);
    };
    connect(m_tileViewPage,    &TileViewPage::wikiRequested,       this, showWiki);
    connect(m_composerPage,    &PromptComposerPage::wikiRequested, this, showWiki);
    connect(m_facetEditorPage, &FacetEditorPage::wikiRequested,    this, showWiki);
    connect(m_wikiPage,        &TagWikiPage::wikiLinkClicked,      this, showWiki);

    // ── Facet editor navigation ───────────────────────────────────────────────
    auto showFacetEditor = [this](const QString& tag) {
        m_pages->setCurrentIndex(3);
        m_facetEditorPage->selectTagByName(tag);
    };
    connect(m_tileViewPage, &TileViewPage::facetEditorRequested,       this, showFacetEditor);
    connect(m_composerPage, &PromptComposerPage::facetEditorRequested, this, showFacetEditor);

    // ── Workflow editor navigation ────────────────────────────────────────────
    connect(m_composerPage, &PromptComposerPage::workflowEditorRequested, this, [this]() {
        m_workflowEditPage->refresh();
        m_pages->setCurrentIndex(5);
    });
    connect(m_composerPage, &PromptComposerPage::workflowVarsChanged,
            m_workflowEditPage, &WorkflowEditPage::refresh);

    // ── Run with workflow: apply vars + positive tags, queue to ComfyUI ──────
    connect(m_composerPage, &PromptComposerPage::runRequested, this, [this](int count) {
        const core::WorkflowFile* wf = m_workflowManager.selectedFile();
        if (!wf) return;
        QFile f(wf->path);
        if (!f.open(QIODevice::ReadOnly)) return;
        const QString tmpl           = QString::fromUtf8(f.readAll());
        const QString positivePrompt = m_composerPage->currentPromptString(true);
        for (int i = 0; i < count; ++i) {
            QString json = m_workflowManager.applyToJson(tmpl);
            json.replace("__positive__", positivePrompt);
            core::WorkflowManager::applyLoraStack(json, m_activeLoraStack, m_settings.loraBaseDir);
            m_comfyClient->queuePrompt(json);
        }
        m_workflowManager.saveToFile(BASE_PATH + "/" + WORKFLOWS_PATH);
        m_workflowEditPage->refresh();
    });

    // ── Shift+cancel = clear pending queue ────────────────────────────────────
    connect(m_composerPage, &PromptComposerPage::clearPendingRequested,
            this, [this]() {
                m_skipFinalOnPendingClear = true;
                m_comfyClient->clearPending();
            });

    // ── Settings page ─────────────────────────────────────────────────────────
    connect(m_settingsPage, &SettingsPage::settingsChanged, this, [this]() {
        applyComfySettings();
        if (m_settings.danmakuEnabled) {
            m_danmakuOverlay->setGeometry(m_pages->rect());
            m_danmakuOverlay->lower();
        }
        m_danmakuOverlay->setActive(m_settings.danmakuEnabled);
        m_composerPage->setQuickFacets(
            m_settings.quickCharacterFacet, m_settings.quickCopyrightFacet);
        m_tileViewPage->setLoraBaseDir(m_settings.loraBaseDir);
    });
    connect(m_settingsPage, &SettingsPage::reconnectRequested, this, [this]() {
        m_comfyClient->connectToServer();
    });

    // Import / Export dialogs launched from Settings → DATA section.
    connect(m_settingsPage, &SettingsPage::exportEntriesRequested, this, [this]() {
        ExportDialog dlg(m_entryModel, &m_facetIndex, this);
        dlg.exec();
    });
    connect(m_settingsPage, &SettingsPage::importEntriesRequested, this, [this]() {
        ImportDialog dlg(m_entryModel, &m_facetIndex,
                         BASE_PATH + "/data/entry",
                         BASE_PATH + "/" + DEFINITIONS_PATH, this);
        if (dlg.exec() == QDialog::Accepted) {
            // Pick up new entries in the tile view, and refresh facet editor +
            // composer so freshly-added/merged definitions take effect now.
            m_tileViewPage->refreshEntries();
            reloadFacets();
        }
    });

    connect(m_comfyClient, &core::ComfyUiClient::connected, this, [this]() {
        m_settingsPage->setComfyStatus(true);
    });
    connect(m_comfyClient, &core::ComfyUiClient::disconnected, this, [this]() {
        m_settingsPage->setComfyStatus(false);
    });
    connect(m_comfyClient, &core::ComfyUiClient::connectionError, this, [this](const QString& err) {
        m_settingsPage->setComfyStatus(false, err);
    });
    connect(m_comfyClient, &core::ComfyUiClient::previewImageReady,
            m_composerPage, &PromptComposerPage::setPreviewImage);
    connect(m_comfyClient, &core::ComfyUiClient::queueCountChanged,
            m_composerPage, &PromptComposerPage::setQueueCount);
    connect(m_comfyClient, &core::ComfyUiClient::queueCountChanged,
            this, [this](int count) {
                const bool jobFinished = (count < m_lastQueueCount);
                m_lastQueueCount = count;

                if (count <= 0) m_statusBar->clearProgress();

                // Each queue decrement = a prompt just completed. Load its
                // decoded output if we observed progress for it. Skip when the
                // user just interrupted / cleared — that prompt didn't finish
                // so any "newest" temp image is stale. Small delay gives
                // ComfyUI time to write the file before we scan.
                if (jobFinished && m_skipNextFinalLoad) {
                    m_skipNextFinalLoad = false;
                    m_pendingFinalLoad  = false;
                    return;
                }
                if (jobFinished && m_skipFinalOnPendingClear) {
                    m_skipFinalOnPendingClear = false;
                    return;  // running prompt still in flight; keep pending state
                }
                if (jobFinished && m_pendingFinalLoad) {
                    m_pendingFinalLoad = false;
                    QTimer::singleShot(500, this, [this]() { loadFinalPreview(); });
                }
            });
    connect(m_comfyClient, &core::ComfyUiClient::previewProgressChanged,
            this, [this](int step, int total) {
                m_statusBar->setProgress(step, total);
                if (step > 0 && total > 0)
                    m_pendingFinalLoad = true;
            });
    connect(m_composerPage, &PromptComposerPage::interruptRequested,
            this, [this]() {
                m_skipNextFinalLoad = true;
                m_comfyClient->interrupt();
            });

    // ── Layout ────────────────────────────────────────────────────────────────
    QWidget* content = new QWidget(this);
    auto* hLayout = new QHBoxLayout(content);
    hLayout->setContentsMargins(0, 0, 0, 0);
    hLayout->setSpacing(0);
    hLayout->addWidget(nav);
    hLayout->addWidget(m_pages, 1);

    m_statusBar = new StatusBar(this);

    QWidget* central = new QWidget(this);
    auto* vLayout = new QVBoxLayout(central);
    vLayout->setContentsMargins(0, 0, 0, 0);
    vLayout->setSpacing(0);
    vLayout->addWidget(content, 1);
    vLayout->addWidget(m_statusBar);

    connect(m_composerPage, &PromptComposerPage::statusMessageRequested,
            m_statusBar,    &StatusBar::showMessage);
    connect(m_tileViewPage, &TileViewPage::statusMessageRequested,
            m_statusBar,    &StatusBar::showMessage);

    setCentralWidget(central);

    // ── Background: load DanbooruIndex ────────────────────────────────────────
    const QString csvPath = BASE_PATH + "/" + DANBOORU_CSV_PATH;
    auto* watcher = new QFutureWatcher<core::DanbooruIndex*>(this);
    connect(watcher, &QFutureWatcher<core::DanbooruIndex*>::finished, this,
        [this, watcher]() {
            m_danbooruIndex = watcher->result();
            m_tileViewPage->setDanbooruIndex(m_danbooruIndex);
            m_composerPage->setDanbooruIndex(m_danbooruIndex);
            m_wikiPage->setDanbooruIndex(m_danbooruIndex);
            watcher->deleteLater();
        });
    watcher->setFuture(QtConcurrent::run([csvPath]() {
        return core::DanbooruIndex::loadFromFile(csvPath);
    }));

    // Load and concatenate every .qss under :/styles. app.qss sorts first so
    // its app-wide rules act as the base; subdir files override as needed.
    QStringList qssPaths;
    QDirIterator qssIt(":/styles", { "*.qss" }, QDir::Files, QDirIterator::Subdirectories);
    while (qssIt.hasNext()) qssPaths << qssIt.next();
    qssPaths.sort();

    QString combinedQss;
    for (const QString& path : qssPaths) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly))
            combinedQss += QString::fromUtf8(f.readAll()) + '\n';
    }
    qApp->setStyleSheet(combinedQss);

    // ── Global keyboard shortcuts ─────────────────────────────────────────────
    // Fired app-wide; guard skips action when a text input has keyboard focus.
    auto textInput = []() -> bool {
        const QWidget* fw = qApp->focusWidget();
        return fw && (qobject_cast<const QLineEdit*>(fw) ||
                      qobject_cast<const QPlainTextEdit*>(fw));
    };
    auto sc = [this](QKeySequence key, auto fn) {
        auto* s = new QShortcut(key, this);
        // Per-window so shortcuts on top-level child windows (e.g., the
        // preview popout) can register the same keys without ambiguity —
        // each window's shortcut fires only when it has focus.
        s->setContext(Qt::WindowShortcut);
        connect(s, &QShortcut::activated, this, fn);
    };

    sc(QKeySequence("Shift+E"), [this, textInput]() {
        if (textInput()) return;
        m_composerPage->triggerRun();
    });
    sc(QKeySequence("Shift+R"), [this, textInput]() {
        if (textInput()) return;
        m_skipNextFinalLoad = true;
        m_comfyClient->interrupt();
    });
    sc(QKeySequence("Shift+Alt+R"), [this, textInput]() {
        if (textInput()) return;
        m_skipFinalOnPendingClear = true;
        m_comfyClient->clearPending();
    });

    sc(QKeySequence("F11"), [this]() {
        m_isFullScreen = !m_isFullScreen;
        if (m_isFullScreen)
            showFullScreen();
        else
            showNormal();
    });

    sc(QKeySequence("Ctrl+W"), [this]() {
        close();
    });

    sc(QKeySequence(Qt::CTRL | Qt::Key_PageUp), [this]() {
        const int cur = m_pages->currentIndex();
        m_pages->setCurrentIndex(cur > 0 ? cur - 1 : m_pages->count() - 1);
    });
    sc(QKeySequence(Qt::CTRL | Qt::Key_PageDown), [this]() {
        const int cur = m_pages->currentIndex();
        m_pages->setCurrentIndex(cur < m_pages->count() - 1 ? cur + 1 : 0);
    });

    QTimer::singleShot(500, this, [this]() {
        show();
        propertyAnimate(this, "windowOpacity", 0.0, 1.0, 500, QEasingCurve::InOutSine);
        m_composerPage->restoreSession(BASE_PATH + "/" + SESSION_PATH);

        if (m_settings.danmakuEnabled) {
            m_danmakuOverlay->setGeometry(m_pages->rect());
            m_danmakuOverlay->lower();
            m_danmakuOverlay->setActive(true);
        }

        if (m_settings.comfyUiEnabled)
            m_comfyClient->connectToServer();
    });
}

bool AppMainWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_pages && event->type() == QEvent::Resize && m_danmakuOverlay) {
        m_danmakuOverlay->setGeometry(m_pages->rect());
        m_danmakuOverlay->lower();
    }
    return QObject::eventFilter(obj, event);
}

void AppMainWindow::applyComfySettings()
{
    m_comfyClient->setServerAddress(m_settings.comfyUiServerAddress);
    m_comfyClient->setApiKey(m_settings.comfyUiApiKey);
    m_composerPage->setOutputFolderPattern(m_settings.comfyUiOutputFolder);
    m_composerPage->setTempFolder(m_settings.comfyUiTempFolder);

    if (m_settings.comfyUiEnabled) {
        m_comfyClient->connectToServer();
    } else {
        m_comfyClient->disconnectFromServer();
    }
}

void AppMainWindow::reloadFacets()
{
    // Persist on every facet mutation rather than relying on close-event save.
    // Cheap (text file, ~few hundred lines) and keeps the on-disk state aligned
    // with what the user just did, even across crashes.
    m_facetIndex.saveDefinitions(BASE_PATH + "/" + DEFINITIONS_PATH);
    m_facetEditorPage->reload();
    m_composerPage->repush();
}

// ── Batch run ───────────────────────────────────────────────────────────────
// Fire-and-forget: resolve query → for each entry build prompt (composer
// state ∪ entry tags) → queue `count` prompts to ComfyUI per entry. Returns
// immediately; ComfyUI handles the actual generation in its own queue.

void AppMainWindow::runBatch(const QString& query)
{
    const core::WorkflowFile* wf = m_workflowManager.selectedFile();
    if (!wf) {
        m_statusBar->showMessage("Batch: no workflow selected");
        return;
    }

    const QList<core::Entry*> matched = m_entryModel->filter(query);
    if (matched.isEmpty()) {
        m_statusBar->showMessage(
            QString("Batch: query \"%1\" matched 0 entries").arg(query));
        return;
    }

    QFile f(wf->path);
    if (!f.open(QIODevice::ReadOnly)) {
        m_statusBar->showMessage("Batch: workflow file unreadable");
        return;
    }
    const QString tmpl = QString::fromUtf8(f.readAll());

    const int count = m_composerPage->currentPromptCount();
    int dispatched = 0;
    int skipped    = 0;

    for (core::Entry* entry : matched) {
        QList<QString> tags;
        if (!entry->images.isEmpty())
            tags = m_entryModel->getTags(entry->images[0].tagIds);

        // Skip entries with no tags — they'd all produce the same prompt
        // (just the composer's state), which isn't useful for a batch.
        if (tags.isEmpty()) {
            ++skipped;
            continue;
        }

        // Entry's own tags get unioned with the current composer state, then
        // the full pipeline (rules + vars) runs over the merged set.
        const QString positivePrompt =
            m_composerPage->computePromptWithExtraTags(tags, true);

        // Stack: current LoRAs + entry's own LoRA if it has one.
        QList<core::LoraConfig> stackForEntry = m_activeLoraStack;
        if (entry->lora.has_value())
            stackForEntry << entry->lora.value();

        for (int i = 0; i < count; ++i) {
            QString json = m_workflowManager.applyToJson(tmpl);
            json.replace("__positive__", positivePrompt);
            core::WorkflowManager::applyLoraStack(
                json, stackForEntry, m_settings.loraBaseDir);
            m_comfyClient->queuePrompt(json);
            ++dispatched;
        }
    }

    m_workflowManager.saveToFile(BASE_PATH + "/" + WORKFLOWS_PATH);
    m_workflowEditPage->refresh();

    const QString summary = (skipped > 0)
        ? QString("Dispatched %1 prompts (%2 entries × %3) — %4 skipped")
            .arg(dispatched).arg(matched.size() - skipped).arg(count).arg(skipped)
        : QString("Dispatched %1 prompts (%2 entries × %3)")
            .arg(dispatched).arg(matched.size()).arg(count);

    m_statusBar->showMessage("Batch: " + summary);
    m_workflowEditPage->setBatchResult(summary);
}

void AppMainWindow::loadFinalPreview()
{
    const QString folder = m_settings.comfyUiTempFolder;
    if (folder.isEmpty()) return;

    static const QStringList filters = { "*.png", "*.jpg", "*.jpeg", "*.webp" };
    const QFileInfoList files = QDir(folder).entryInfoList(filters, QDir::Files);
    if (files.isEmpty()) return;

    // Newest by mtime — the run that just finished should have written it.
    const QFileInfo* newest = &files[0];
    for (const QFileInfo& fi : files)
        if (fi.lastModified() > newest->lastModified()) newest = &fi;

    QImage img(newest->absoluteFilePath());
    if (!img.isNull())
        m_composerPage->setPreviewImage(img);
}

void AppMainWindow::closeEvent(QCloseEvent* event)
{
    static bool isClosing{ false };
    if (isClosing) {
        event->accept();
        return;
    }
    event->ignore();
    qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
    isClosing = true;

    m_settings.save(BASE_PATH + "/" + SETTINGS_PATH);
    m_facetIndex.saveDefinitions(BASE_PATH + "/" + DEFINITIONS_PATH);
    m_composerPage->saveSession(BASE_PATH + "/" + SESSION_PATH);

    connect(
        propertyAnimate(this, "windowOpacity", 1.0, 0.0, 500, QEasingCurve::InOutSine),
        &QPropertyAnimation::finished,
        this,
        [this]() {
            // Hide main + force-close any other top-level windows (popout)
            // so they don't linger on the taskbar past the fade.
            hide();
            for (QWidget* w : qApp->topLevelWidgets()) {
                if (w != this && w->isWindow()) {
                    w->setAttribute(Qt::WA_DeleteOnClose, false);
                    w->hide();
                    w->deleteLater();
                }
            }
            qApp->quit();
        }
    );
}

} // namespace gui
