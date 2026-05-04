#include <gui/appmainwindow.h>
#include <gui/widgets/navbar.h>
#include <gui/widgets/windowchrome.h>
#include <gui/homepage.h>
#include <gui/tileview/tileviewpage.h>
#include <gui/widgets/statusbar.h>
#include <gui/composer/promptcomposerpage.h>
#include <gui/faceteditorpage.h>
#include <gui/tagwikipage.h>
#include <gui/settingspage.h>
#include <gui/workfloweditpage.h>
#include <gui/outputviewerpage.h>
#include <gui/dataset/datasethelperspage.h>
#include <gui/dataset/tagclusterpage.h>
#include <gui/dataset/tageditorpage.h>
#include <gui/dataset/collectorpage.h>
#include <gui/homepage.h>
#include <core/updatechecker.h>
#include <QDateTime>
#include <gui/widgets/danmakuoverlay.h>
#include <gui/exportdialog.h>
#include <gui/importdialog.h>
#include <utils/qutils.h>
#include <utils/appconfig.h>
#include <QApplication>
#include <QEvent>
#include <QWindowStateChangeEvent>
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
    : QMainWindow{parent}, m_entryModel{new core::EntryModel(this)}
{
    setWindowTitle("Tag Composer");
    setWindowOpacity(0.0);

    // Frameless: we draw our own titlebar (gui::TitleBar) and a thin border
    // around the central frame for resize hit-testing. The Min/Max/Close
    // hints stay set so the OS still treats us as a normal app window in the
    // taskbar (snap, animations, alt-tab) even with no native chrome.
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinimizeButtonHint |
                   Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);

    // Initial size + floor. Most window managers honor these; tiling WMs
    // ignore them and tile the window however they prefer.
    resize(1420, 920);
    setMinimumSize(1420, 920);

    // ── Load settings ─────────────────────────────────────────────────────────
    m_settings = AppSettings::load(BASE_PATH + "/" + SETTINGS_PATH);

    // Seed the connection cache so the first settingsChanged emit (which fires
    // the moment the user touches *anything* on the settings page) doesn't see
    // "host went from empty to <real host>" and trigger a spurious reconnect.
    m_lastComfyEnabled = m_settings.comfyUiEnabled;
    m_lastComfyHost = m_settings.comfyUiServerAddress;
    m_lastComfyApiKey = m_settings.comfyUiApiKey;

    // ── ComfyUI client ────────────────────────────────────────────────────────
    m_comfyClient = new core::ComfyUiClient(this);
    m_comfyClient->setServerAddress(m_settings.comfyUiServerAddress);
    m_comfyClient->setApiKey(m_settings.comfyUiApiKey);

    // ── Workflow input cache ──────────────────────────────────────────────────
    m_inputCache = new core::WorkflowInputCache(BASE_PATH + "/" + WORKFLOW_INPUTS_DIR, this);
    // Forget upload state on (re)connect so a server restart re-uploads inputs.
    connect(m_comfyClient, &core::ComfyUiClient::connected, this,
            [this]() { m_uploadedThisSession.clear(); });
    connect(m_comfyClient, &core::ComfyUiClient::disconnected, this,
            [this]() { m_uploadedThisSession.clear(); });

    // ── Load pipeline data ────────────────────────────────────────────────────
    m_facetIndex = core::FacetIndex::loadFromFile(BASE_PATH + "/" + FACETS_PATH);
    m_facetIndex.loadDefinitionsFromFile(BASE_PATH + "/" + DEFINITIONS_PATH);
    m_ruleEngine = core::RuleEngine::loadFromFile(BASE_PATH + "/" + RULES_PATH);
    m_tagGroupIndex = core::TagGroupIndex::loadFromFile(BASE_PATH + "/" + GROUPS_PATH);
    m_varIndex = core::VariableIndex::loadFromFile(BASE_PATH + "/" + VARS_PATH);
    m_workflowManager = core::WorkflowManager::loadFromFile(BASE_PATH + "/" + WORKFLOWS_PATH);
    m_pipeline = new core::PromptPipeline(&m_facetIndex, &m_ruleEngine, &m_varIndex, this);

    // ── Pages ─────────────────────────────────────────────────────────────────
    m_tileViewPage = new TileViewPage(m_entryModel, this);
    m_tileViewPage->setLoraBaseDir(m_settings.loraBaseDir);
    m_tileViewPage->setComfyClient(m_comfyClient);
    // Tile gradient + title colour are read once at startup; mid-session
    // edits in Settings are persisted but require a restart to take effect.
    m_tileViewPage->setTileGradient(m_settings.tileGradientStart, m_settings.tileGradientAlpha);
    m_tileViewPage->setTileTitleColor(QColor(m_settings.tileTitleColor));
    m_composerPage = new PromptComposerPage(m_pipeline, &m_ruleEngine, m_tagGroupIndex, this);
    m_composerPage->setVariableIndex(&m_varIndex);
    m_composerPage->setWorkflowManager(&m_workflowManager, BASE_PATH + "/" + WORKFLOWS_PATH);
    m_composerPage->setStatesDir(BASE_PATH + "/" + STATES_DIR);
    m_composerPage->setEntryModel(m_entryModel);
    m_composerPage->setInputCache(m_inputCache);
    m_composerPage->setOutputFolderPattern(m_settings.comfyUiOutputFolder);
    m_composerPage->setTempFolder(m_settings.comfyUiTempFolder);
    m_composerPage->setQuickFacets(m_settings.quickCharacterFacet, m_settings.quickCopyrightFacet,
                                   m_settings.quickTriggerWordFacet, m_settings.quickStyleFacet);
    m_tileViewPage->setQuickFacets(m_settings.quickCharacterFacet, m_settings.quickCopyrightFacet,
                                   m_settings.quickTriggerWordFacet, m_settings.quickStyleFacet);
    m_facetEditorPage = new FacetEditorPage(&m_facetIndex, m_entryModel, this);
    m_facetEditorPage->setVariableIndex(&m_varIndex);
    m_facetEditorPage->setActiveTagsProvider(
        [this]() { return m_composerPage->currentActiveTags(); });

    m_wikiPage = new TagWikiPage(this);
    m_settingsPage = new SettingsPage(&m_settings, this);
    m_workflowEditPage = new WorkflowEditPage(this);
    m_workflowEditPage->setWorkflowManager(&m_workflowManager, BASE_PATH + "/" + WORKFLOWS_PATH);
    m_workflowEditPage->setEntryModel(m_entryModel);
    m_workflowEditPage->setInputCache(m_inputCache);

    m_outputViewerPage = new OutputViewerPage(this);
    m_outputViewerPage->setOutputFolder(m_settings.comfyUiOutputFolder);

    m_pages = new QStackedWidget(this);
    m_pages->setObjectName("MainPages");
    m_pages->installEventFilter(this);
    // Order must match gui::Page enum (navbar.h) and the navbar's addButton
    // sequence. Adding/reordering pages = touch all three together.
    m_homePage = new HomePage(this);
    m_pages->addWidget(m_homePage);         // Page::Home
    m_pages->addWidget(m_tileViewPage);     // Page::EntryViewer
    m_pages->addWidget(m_composerPage);     // Page::TagComposer
    m_pages->addWidget(m_facetEditorPage);  // Page::FacetEditor
    m_pages->addWidget(m_workflowEditPage); // Page::WorkflowEditor
    m_pages->addWidget(m_outputViewerPage); // Page::OutputViewer
    m_taggerLibrary = std::make_unique<core::AutoTaggerLibrary>(BASE_PATH + "/" + MODELS_DIR);
    // Settle on a default active model if the user hasn't picked one yet -
    // first available wins. AutoTagPage and any other consumer reads
    // AppSettings::activeAutoTagModel from there.
    if (m_settings.activeAutoTagModel.isEmpty()) {
        const auto names = m_taggerLibrary->availableModels();
        if (!names.isEmpty()) m_settings.activeAutoTagModel = names.first();
    }

    m_datasetHelpersPage =
        new DatasetHelpersPage(&m_facetIndex, m_taggerLibrary.get(), &m_settings, this);
    m_pages->addWidget(m_datasetHelpersPage); // Page::DatasetHelpers
    m_pages->addWidget(m_wikiPage);           // Page::DanbooruWiki
    m_pages->addWidget(m_settingsPage);       // Page::Settings

    // ── Danmaku overlay (behind all pages) ────────────────────────────────────
    m_danmakuOverlay = new DanmakuOverlay(m_pages);

    NavBar* nav = new NavBar(this, this);
    connect(nav, &NavBar::pageRequested, m_pages, &QStackedWidget::setCurrentIndex);
    connect(m_pages, &QStackedWidget::currentChanged, nav, &NavBar::setCurrentPage);

    // ── LoRA stack: tile view → main window + composer + workflow editor ─────
    connect(m_tileViewPage, &TileViewPage::loraStackChanged, this,
            [this](const QList<core::LoraConfig>& stack) {
                m_activeLoraStack = stack;
                m_activeLoraUuids = m_tileViewPage->activeLoraUuids();
                m_composerPage->setActiveLoraUuids(m_activeLoraUuids);
                m_workflowEditPage->setActiveLoraStack(stack);
            });

    // Strength edits in the workflow editor mutate the entries directly;
    // mirror the new values into our cached stack so the next workflow run
    // uses them without waiting for a tile-view re-emit.
    connect(m_workflowEditPage, &WorkflowEditPage::loraStrengthsChanged, this,
            [this](const QList<core::LoraConfig>& stack) { m_activeLoraStack = stack; });

    // ── Batch ─────────────────────────────────────────────────────────────────
    connect(m_workflowEditPage, &WorkflowEditPage::batchRunRequested, this,
            &AppMainWindow::runBatch);

    // ── LoRA restore: composer session/state → tile view ─────────────────────
    connect(m_composerPage, &PromptComposerPage::loraUuidsRestored, m_tileViewPage,
            &TileViewPage::setLoraActiveByUuids);

    // ── Wire export: extraBtn → composer page ─────────────────────────────────
    connect(m_tileViewPage, &TileViewPage::tagsExported, m_composerPage,
            &PromptComposerPage::loadPipeline);
    connect(m_tileViewPage, &TileViewPage::entryTagAdded, m_composerPage,
            &PromptComposerPage::onEntryTagAdded);
    connect(m_tileViewPage, &TileViewPage::entryTagRemoved, m_composerPage,
            &PromptComposerPage::onEntryTagRemoved);

    // ── Entry deletion: drop pushes/lora referencing it from composer ──────────
    connect(m_entryModel, &core::EntryModel::entryDeleted, m_composerPage,
            &PromptComposerPage::onEntryDeleted);

    // ── Sync push-group state back to tile view ────────────────────────────────
    connect(m_composerPage, &PromptComposerPage::activeGroupsChanged, m_tileViewPage,
            &TileViewPage::setActiveGroups);

    // ── Wire facet editor reload ───────────────────────────────────────────────
    connect(m_facetEditorPage, &FacetEditorPage::facetsDefined, this, &AppMainWindow::reloadFacets);

    // Schema reload - re-read facets.fct, keep tag definitions intact
    connect(m_facetEditorPage, &FacetEditorPage::schemaReloadRequested, this, [this]() {
        m_facetIndex.reloadSchemaFromFile(BASE_PATH + "/" + FACETS_PATH);
        reloadFacets();
        m_statusBar->showMessage("Facet schema reloaded.");
    });

    // ── Quick-add facet shortcut ──────────────────────────────────────────────
    // Right-click → "Quick add as <kind>" mutates the FacetIndex here
    // (the panels don't own the index), persists, and reloads. Both the
    // composer and the entry panel emit the same (tag, facetName) signal.
    connect(m_composerPage, &PromptComposerPage::quickFacetRequested, this,
            &AppMainWindow::applyQuickFacet);
    connect(m_tileViewPage, &TileViewPage::quickFacetRequested, this,
            &AppMainWindow::applyQuickFacet);
    if (auto* tcp = m_datasetHelpersPage->tagClusterPage()) {
        connect(tcp, &TagClusterPage::quickFacetRequested, this, &AppMainWindow::applyQuickFacet);
        tcp->setQuickFacets(m_settings.quickCharacterFacet, m_settings.quickCopyrightFacet,
                            m_settings.quickTriggerWordFacet, m_settings.quickStyleFacet);
    }

    // ── Wiki page navigation ───────────────────────────────────────────────────
    auto showWiki = [this](const QString& tag) {
        m_wikiPage->lookupTag(tag);
        m_pages->setCurrentIndex(int(Page::DanbooruWiki));
    };
    connect(m_tileViewPage, &TileViewPage::wikiRequested, this, showWiki);
    connect(m_composerPage, &PromptComposerPage::wikiRequested, this, showWiki);
    connect(m_facetEditorPage, &FacetEditorPage::wikiRequested, this, showWiki);
    connect(m_wikiPage, &TagWikiPage::wikiLinkClicked, this, showWiki);
    if (auto* tcp = m_datasetHelpersPage->tagClusterPage())
        connect(tcp, &TagClusterPage::wikiRequested, this, showWiki);

    // ── Facet editor navigation ───────────────────────────────────────────────
    auto showFacetEditor = [this](const QString& tag) {
        m_pages->setCurrentIndex(3);
        m_facetEditorPage->selectTagByName(tag);
    };
    connect(m_tileViewPage, &TileViewPage::facetEditorRequested, this, showFacetEditor);
    connect(m_composerPage, &PromptComposerPage::facetEditorRequested, this, showFacetEditor);
    if (auto* tcp = m_datasetHelpersPage->tagClusterPage())
        connect(tcp, &TagClusterPage::facetEditorRequested, this, showFacetEditor);

    // ── Workflow editor navigation ────────────────────────────────────────────
    connect(m_composerPage, &PromptComposerPage::workflowEditorRequested, this, [this]() {
        m_workflowEditPage->refresh();
        m_pages->setCurrentIndex(int(Page::WorkflowEditor));
    });
    connect(m_composerPage, &PromptComposerPage::workflowVarsChanged, m_workflowEditPage,
            &WorkflowEditPage::refresh);

    // ── Run with workflow: apply vars + positive tags, queue to ComfyUI ──────
    // Positive prompt is recomputed inside the loop because Wildcard vars pick
    // a fresh tag per run, and that pick must flow through the rule/var
    // pipeline along with the composer's persistent state.
    connect(m_composerPage, &PromptComposerPage::runRequested, this, [this](int count) {
        const core::WorkflowFile* wf = m_workflowManager.selectedFile();
        if (!wf) return;
        QFile f(wf->path);
        if (!f.open(QIODevice::ReadOnly)) return;
        const QString tmpl = QString::fromUtf8(f.readAll());

        ensureImageInputsUploaded([this, tmpl, count]() {
            for (int i = 0; i < count; ++i) {
                const QStringList wildTags = m_workflowManager.pickWildcardTags();
                const QString positivePrompt =
                    wildTags.isEmpty() ? m_composerPage->currentPromptString(true)
                                       : m_composerPage->computePromptWithExtraTags(wildTags, true);

                QString json = m_workflowManager.applyToJson(tmpl);
                core::WorkflowManager::applyPositive(json, positivePrompt);
                core::WorkflowManager::applyLoraStack(json, m_activeLoraStack,
                                                      m_settings.loraBaseDir);
                m_comfyClient->queuePrompt(json);
            }
            m_workflowManager.saveToFile(BASE_PATH + "/" + WORKFLOWS_PATH);
            m_workflowEditPage->refresh();
        });
    });

    // ── Shift+cancel = clear pending queue ────────────────────────────────────
    connect(m_composerPage, &PromptComposerPage::clearPendingRequested, this, [this]() {
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
            m_settings.quickCharacterFacet, m_settings.quickCopyrightFacet,
            m_settings.quickTriggerWordFacet, m_settings.quickStyleFacet);
        m_tileViewPage->setQuickFacets(
            m_settings.quickCharacterFacet, m_settings.quickCopyrightFacet,
            m_settings.quickTriggerWordFacet, m_settings.quickStyleFacet);
        if (auto* tcp = m_datasetHelpersPage->tagClusterPage()) {
            tcp->setQuickFacets(m_settings.quickCharacterFacet, m_settings.quickCopyrightFacet,
                                m_settings.quickTriggerWordFacet, m_settings.quickStyleFacet);
        }
        m_tileViewPage->setLoraBaseDir(m_settings.loraBaseDir);
    });
    connect(m_settingsPage, &SettingsPage::reconnectRequested, this,
            [this]() { m_comfyClient->connectToServer(); });

    // Import / Export dialogs launched from Settings → DATA section.
    connect(m_settingsPage, &SettingsPage::exportEntriesRequested, this, [this]() {
        ExportDialog dlg(m_entryModel, &m_facetIndex, this);
        dlg.exec();
    });
    connect(m_settingsPage, &SettingsPage::importEntriesRequested, this, [this]() {
        ImportDialog dlg(m_entryModel, &m_facetIndex, BASE_PATH + "/data/entry",
                         BASE_PATH + "/" + DEFINITIONS_PATH, this);
        if (dlg.exec() == QDialog::Accepted) {
            // Pick up new entries in the tile view, and refresh facet editor +
            // composer so freshly-added/merged definitions take effect now.
            m_tileViewPage->refreshEntries();
            reloadFacets();
        }
    });

    connect(m_settingsPage, &SettingsPage::clearUnusedInputsRequested, this,
            &AppMainWindow::clearUnusedInputs);

    connect(m_comfyClient, &core::ComfyUiClient::connected, this,
            [this]() { m_settingsPage->setComfyStatus(true); });
    connect(m_comfyClient, &core::ComfyUiClient::disconnected, this,
            [this]() { m_settingsPage->setComfyStatus(false); });
    connect(m_comfyClient, &core::ComfyUiClient::connectionError, this,
            [this](const QString& err) { m_settingsPage->setComfyStatus(false, err); });
    connect(m_comfyClient, &core::ComfyUiClient::previewImageReady, m_composerPage,
            &PromptComposerPage::setPreviewImage);
    connect(m_comfyClient, &core::ComfyUiClient::queueCountChanged, this, [this](int count) {
        // Routed through a lambda because m_statusBar is constructed
        // further down in this ctor; the receiver pointer would be
        // null at connect time if we wired the signal directly.
        m_statusBar->setActiveCount(count);

        const bool jobFinished = (count < m_lastQueueCount);
        m_lastQueueCount = count;

        // Progress bar fade-out is owned by StatusBar::setActiveCount
        // (called above), so the bar and the active-count label drop
        // together when the queue drains.

        // Each queue decrement = a prompt just completed. Load its
        // decoded output if we observed progress for it. Skip when the
        // user just interrupted / cleared - that prompt didn't finish
        // so any "newest" temp image is stale. Small delay gives
        // ComfyUI time to write the file before we scan.
        if (jobFinished && m_skipNextFinalLoad) {
            m_skipNextFinalLoad = false;
            m_pendingFinalLoad = false;
            return;
        }
        if (jobFinished && m_skipFinalOnPendingClear) {
            m_skipFinalOnPendingClear = false;
            return; // running prompt still in flight; keep pending state
        }
        if (jobFinished && m_pendingFinalLoad) {
            m_pendingFinalLoad = false;
            QTimer::singleShot(500, this, [this]() { loadFinalPreview(); });
        }
    });
    connect(m_comfyClient, &core::ComfyUiClient::previewProgressChanged, this,
            [this](int step, int total) {
                m_statusBar->setProgress(step, total);
                if (step > 0 && total > 0) m_pendingFinalLoad = true;
            });
    connect(m_composerPage, &PromptComposerPage::interruptRequested, this, [this]() {
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

    // Frameless chrome: titlebar + cosmetic border + edge-resize, all owned
    // by WindowChrome. The body widget hosts the actual app content (nav +
    // pages + statusbar). Top-level window so all three chrome buttons.
    m_chrome = new WindowChrome(this);

    auto* bodyLayout = new QVBoxLayout(m_chrome->bodyWidget());
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    bodyLayout->addWidget(content, 1);
    bodyLayout->addWidget(m_statusBar);

    connect(m_composerPage, &PromptComposerPage::statusMessageRequested, m_statusBar,
            &StatusBar::showMessage);
    connect(m_tileViewPage, &TileViewPage::statusMessageRequested, m_statusBar,
            &StatusBar::showMessage);

    setCentralWidget(m_chrome->frame());

    // ── Background: load DanbooruIndex ────────────────────────────────────────
    const QString csvPath = BASE_PATH + "/" + DANBOORU_CSV_PATH;
    auto* watcher = new QFutureWatcher<core::DanbooruIndex*>(this);
    connect(watcher, &QFutureWatcher<core::DanbooruIndex*>::finished, this, [this, watcher]() {
        m_danbooruIndex = watcher->result();
        m_tileViewPage->setDanbooruIndex(m_danbooruIndex);
        m_composerPage->setDanbooruIndex(m_danbooruIndex);
        m_wikiPage->setDanbooruIndex(m_danbooruIndex);
        m_facetEditorPage->setDanbooruIndex(m_danbooruIndex);
        if (auto* tep = m_datasetHelpersPage->tagEditorPage())
            tep->setDanbooruIndex(m_danbooruIndex);
        watcher->deleteLater();
    });
    watcher->setFuture(
        QtConcurrent::run([csvPath]() { return core::DanbooruIndex::loadFromFile(csvPath); }));

    // Load and concatenate every .qss under :/styles. app.qss sorts first so
    // its app-wide rules act as the base; subdir files override as needed.
    QStringList qssPaths;
    QDirIterator qssIt(":/styles", {"*.qss"}, QDir::Files, QDirIterator::Subdirectories);
    while (qssIt.hasNext())
        qssPaths << qssIt.next();
    qssPaths.sort();

    QString combinedQss;
    for (const QString& path : qssPaths) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) combinedQss += QString::fromUtf8(f.readAll()) + '\n';
    }
    qApp->setStyleSheet(combinedQss);

    // ── Global keyboard shortcuts ─────────────────────────────────────────────
    // Fired app-wide; guard skips action when a text input has keyboard focus.
    auto textInput = []() -> bool {
        const QWidget* fw = qApp->focusWidget();
        return fw &&
               (qobject_cast<const QLineEdit*>(fw) || qobject_cast<const QPlainTextEdit*>(fw));
    };
    auto sc = [this](QKeySequence key, auto fn) {
        auto* s = new QShortcut(key, this);
        // Per-window so shortcuts on top-level child windows (e.g., the
        // preview popout) can register the same keys without ambiguity -
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
        auto* anim = utils::propertyAnimate(this, "windowOpacity", windowOpacity(), 0.0, 200,
                                            QEasingCurve::InOutSine);
        connect(anim, &QPropertyAnimation::finished, this, [this]() {
            if (isFullScreen())
                showNormal();
            else
                showFullScreen();
            utils::propertyAnimate(this, "windowOpacity", windowOpacity(), 1.0, 200,
                                   QEasingCurve::InOutSine);
        });
    });

    sc(QKeySequence("Ctrl+W"), [this]() { close(); });

    sc(QKeySequence("Ctrl+H"), [this]() {
        if (windowState() & Qt::WindowMinimized) return;
        auto* anim = utils::propertyAnimate(this, "windowOpacity", windowOpacity(), 0.0, 200,
                                            QEasingCurve::InOutSine);
        connect(anim, &QPropertyAnimation::finished, this, [this]() { showMinimized(); });
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

        if (m_settings.comfyUiEnabled) m_comfyClient->connectToServer();
    });

    // ── Update check ──────────────────────────────────────────────────────────
    // Once-per-launch GitHub Releases poll, throttled to ~24h via the
    // lastUpdateCheckTime setting. Not blocking - kicked off 2 s after the
    // window appears so it doesn't compete with the rest of boot work.
    m_updateChecker = new core::UpdateChecker(this);
    m_updateChecker->setRepo("typeRYOON/tagcomposer_test");
    m_updateChecker->setCurrentVersion(APP_VERSION);

    connect(m_updateChecker, &core::UpdateChecker::updateAvailable, this,
            [this](QString latest, QString url) {
                m_settings.lastUpdateCheckTime = QDateTime::currentSecsSinceEpoch();
                m_settings.lastKnownLatestVersion = latest;
                if (m_homePage) m_homePage->setUpdateAvailable(latest, url);
            });
    connect(m_updateChecker, &core::UpdateChecker::upToDate, this, [this](QString latest) {
        m_settings.lastUpdateCheckTime = QDateTime::currentSecsSinceEpoch();
        m_settings.lastKnownLatestVersion = latest;
        if (m_homePage) m_homePage->setUpdateAvailable({});
    });
    // checkFailed: leave lastUpdateCheckTime alone so we retry next launch
    // instead of waiting another 24h after a transient network blip.

    // If we've already checked within the last 24h, surface the cached result
    // (so the "update available" label reappears across restarts without
    // re-pinging GitHub) and skip the network call this session.
    constexpr qint64 kThrottleSec = 24 * 60 * 60;
    const qint64 nowSec = QDateTime::currentSecsSinceEpoch();
    const bool fresh = (m_settings.lastUpdateCheckTime > 0) &&
                       (nowSec - m_settings.lastUpdateCheckTime < kThrottleSec);

    if (fresh && !m_settings.lastKnownLatestVersion.isEmpty()) {
        // Re-evaluate the cached version against the current build - if the
        // user updated manually since the last check, the cached "newer"
        // verdict is now stale.
        if (core::UpdateChecker::compareVersions(m_settings.lastKnownLatestVersion, APP_VERSION) >
            0) {
            m_homePage->setUpdateAvailable(
                m_settings.lastKnownLatestVersion,
                QString("https://github.com/typeRYOON/tagcomposer_test/releases/latest"));
        }
    }
    else {
        QTimer::singleShot(2000, this, [this]() { m_updateChecker->checkNow(); });
    }
}

bool AppMainWindow::eventFilter(QObject* obj, QEvent* event)
{
    // Danmaku overlay tracks the m_pages widget's size. Chrome events
    // (resize-overlay sync, edge-drag) are handled internally by m_chrome's
    // own event filter on m_chrome->frame() and m_chrome->resizeOverlay().
    if (obj == m_pages && event->type() == QEvent::Resize && m_danmakuOverlay) {
        m_danmakuOverlay->setGeometry(m_pages->rect());
        m_danmakuOverlay->lower();
    }
    return QMainWindow::eventFilter(obj, event);
}

void AppMainWindow::applyComfySettings()
{
    m_comfyClient->setServerAddress(m_settings.comfyUiServerAddress);
    m_comfyClient->setApiKey(m_settings.comfyUiApiKey);
    m_composerPage->setOutputFolderPattern(m_settings.comfyUiOutputFolder);
    m_composerPage->setTempFolder(m_settings.comfyUiTempFolder);
    m_outputViewerPage->setOutputFolder(m_settings.comfyUiOutputFolder);

    // Bouncing the WebSocket on every settingsChanged emit is what causes the
    // status indicator to flicker through disconnected → connecting → connected
    // when the user edits an unrelated field (tile gradient, danmaku toggle,
    // etc.). Only re-(dis)connect when a connection-relevant value changed.
    const bool enabledChanged = (m_settings.comfyUiEnabled != m_lastComfyEnabled);
    const bool hostChanged = (m_settings.comfyUiServerAddress != m_lastComfyHost);
    const bool apiKeyChanged = (m_settings.comfyUiApiKey != m_lastComfyApiKey);
    const bool needsBounce = enabledChanged || hostChanged || apiKeyChanged;

    m_lastComfyEnabled = m_settings.comfyUiEnabled;
    m_lastComfyHost = m_settings.comfyUiServerAddress;
    m_lastComfyApiKey = m_settings.comfyUiApiKey;

    if (!needsBounce) return;

    if (m_settings.comfyUiEnabled) {
        m_comfyClient->connectToServer();
    }
    else {
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

void AppMainWindow::applyQuickFacet(const QString& tag, const QString& facetName)
{
    if (tag.isEmpty() || facetName.isEmpty()) return;
    QList<QString> existing = m_facetIndex.facetsFor(tag);
    if (existing.contains(facetName)) {
        m_statusBar->showMessage(QString("'%1' already has facet '%2'").arg(tag, facetName));
        return;
    }
    existing << facetName;
    m_facetIndex.setDefinition(tag, existing);
    reloadFacets(); // also persists via saveDefinitions
    m_statusBar->showMessage(QString("Added facet '%1' to '%2'").arg(facetName, tag));
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
        m_statusBar->showMessage(QString("Batch: query \"%1\" matched 0 entries").arg(query));
        return;
    }

    QFile f(wf->path);
    if (!f.open(QIODevice::ReadOnly)) {
        m_statusBar->showMessage("Batch: workflow file unreadable");
        return;
    }
    const QString tmpl = QString::fromUtf8(f.readAll());

    const int count = m_composerPage->currentPromptCount();

    ensureImageInputsUploaded([this, matched, tmpl, count]() {
        int dispatched = 0;
        int skipped = 0;

        for (core::Entry* entry : matched) {
            QList<QString> tags;
            if (!entry->images.isEmpty()) tags = m_entryModel->getTags(entry->images[0].tagIds);

            // Skip entries with no tags - they'd all produce the same prompt
            // (just the composer's state), which isn't useful for a batch.
            if (tags.isEmpty()) {
                ++skipped;
                continue;
            }

            // Stack: current LoRAs + entry's own LoRA if it has one.
            QList<core::LoraConfig> stackForEntry = m_activeLoraStack;
            if (entry->lora.has_value()) stackForEntry << entry->lora.value();

            for (int i = 0; i < count; ++i) {
                // Per-run wildcard pick gets unioned with composer state +
                // entry tags, then the full pipeline runs over the merged set.
                QList<QString> extraTags = tags;
                for (const QString& wt : m_workflowManager.pickWildcardTags())
                    extraTags << wt;
                const QString positivePrompt =
                    m_composerPage->computePromptWithExtraTags(extraTags, true);

                QString json = m_workflowManager.applyToJson(tmpl);
                core::WorkflowManager::applyPositive(json, positivePrompt);
                core::WorkflowManager::applyLoraStack(json, stackForEntry, m_settings.loraBaseDir);
                m_comfyClient->queuePrompt(json);
                ++dispatched;
            }
        }

        m_workflowManager.saveToFile(BASE_PATH + "/" + WORKFLOWS_PATH);
        m_workflowEditPage->refresh();

        const QString summary =
            (skipped > 0) ? QString("Dispatched %1 prompts (%2 entries × %3) - %4 skipped")
                                .arg(dispatched)
                                .arg(matched.size() - skipped)
                                .arg(count)
                                .arg(skipped)
                          : QString("Dispatched %1 prompts (%2 entries × %3)")
                                .arg(dispatched)
                                .arg(matched.size())
                                .arg(count);

        m_statusBar->showMessage("Batch: " + summary);
        m_workflowEditPage->setBatchResult(summary);
    });
}

void AppMainWindow::loadFinalPreview()
{
    const QString folder = m_settings.comfyUiTempFolder;
    if (folder.isEmpty()) return;

    static const QStringList filters = {"*.png", "*.jpg", "*.jpeg", "*.webp"};
    const QFileInfoList files = QDir(folder).entryInfoList(filters, QDir::Files);
    if (files.isEmpty()) return;

    // Newest by mtime - the run that just finished should have written it.
    const QFileInfo* newest = &files[0];
    for (const QFileInfo& fi : files)
        if (fi.lastModified() > newest->lastModified()) newest = &fi;

    QImage img(newest->absoluteFilePath());
    if (!img.isNull()) m_composerPage->setPreviewImage(img);
}

// Walk the selected workflow's vars; for each Image var with a known uuid we
// haven't pushed this session, upload it. After every upload settles, run done.
//
// Tracking key is (uuid + editsHash) - editing an already-uploaded image
// produces a different hash, so the rendered variant gets uploaded fresh.
void AppMainWindow::ensureImageInputsUploaded(std::function<void()> done)
{
    struct UploadTask {
        QString trackingKey; // m_uploadedThisSession key
        QString localPath;   // file to upload (may be a rendered edited variant)
    };
    QList<UploadTask> tasks;

    if (m_inputCache && m_workflowManager.selectedFile()) {
        for (const auto& var : m_workflowManager.variables()) {
            if (var.type != core::WorkflowVarType::Image) continue;
            if (var.imageUuid.isEmpty()) continue;
            if (!m_inputCache->has(var.imageUuid)) {
                m_statusBar->showMessage(
                    QString("Image input %1 missing - skipped").arg(var.placeholder));
                continue;
            }
            const QString trackingKey = var.imageUuid + ":" + var.imageEdits.hash();
            if (m_uploadedThisSession.contains(trackingKey)) continue;
            const QString local = m_inputCache->resolveEdited(var.imageUuid, var.imageEdits);
            if (local.isEmpty()) continue;
            tasks << UploadTask{trackingKey, local};
        }
    }

    if (tasks.isEmpty()) {
        if (done) done();
        return;
    }

    auto remaining = std::make_shared<int>(tasks.size());
    auto fired = std::make_shared<bool>(false);
    const QString inputFolder = m_settings.comfyUiInputFolder;

    for (const UploadTask& task : tasks) {
        m_comfyClient->uploadInput(
            task.localPath, core::WorkflowInputCache::serverSubfolder(), inputFolder,
            [this, key = task.trackingKey, remaining, fired, done](bool ok, QString err) {
                if (ok)
                    m_uploadedThisSession.insert(key);
                else if (m_statusBar) {
                    m_statusBar->showMessage(
                        QString("Upload failed (%1): %2").arg(key.left(8), err));
                }
                if (--(*remaining) == 0 && !*fired) {
                    *fired = true;
                    if (done) done();
                }
            });
    }
}

void AppMainWindow::clearUnusedInputs()
{
    if (!m_inputCache) return;

    // 1. Walk every workflow's vars and collect the set of references that
    //    keep cache entries alive. Anything not in these sets is orphaned.
    QSet<QString> usedImageUuids;
    QSet<QString> usedMaskIds;
    QSet<QString> usedEditsHashes;
    for (const core::WorkflowFile& wf : m_workflowManager.files()) {
        for (const core::WorkflowVar& v : wf.vars) {
            if (v.type != core::WorkflowVarType::Image) continue;
            if (!v.imageUuid.isEmpty()) usedImageUuids.insert(v.imageUuid);
            if (!v.imageEdits.maskId.isEmpty()) usedMaskIds.insert(v.imageEdits.maskId);
            if (v.imageEdits.enabled) usedEditsHashes.insert(v.imageEdits.hash());
        }
    }

    int imagesRemoved = 0;
    int masksRemoved = 0;
    int rendersRemoved = 0;

    // 2. Source images. Snapshot all() first since remove() mutates the map.
    for (const core::WorkflowInput& inp : m_inputCache->all()) {
        if (!usedImageUuids.contains(inp.uuid)) {
            m_inputCache->remove(inp.uuid);
            ++imagesRemoved;
        }
    }

    // 3. Painted masks under _masks/<id>.png.
    QDir masksDir(m_inputCache->cacheDir() + "/_masks");
    if (masksDir.exists()) {
        for (const QFileInfo& fi : masksDir.entryInfoList(QStringList{"*.png"}, QDir::Files)) {
            const QString id = fi.completeBaseName();
            if (!usedMaskIds.contains(id)) {
                m_inputCache->removeMask(id);
                ++masksRemoved;
            }
        }
    }

    // 4. Rendered edit variants under _edited/<editsHash>/<uuid>.png. The
    //    directory is per-hash so we just nuke whole subdirectories that no
    //    active edits hash references.
    QDir editedDir(m_inputCache->cacheDir() + "/_edited");
    if (editedDir.exists()) {
        for (const QFileInfo& fi : editedDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (!usedEditsHashes.contains(fi.fileName())) {
                if (QDir(fi.absoluteFilePath()).removeRecursively()) ++rendersRemoved;
            }
        }
    }

    // Drop session-upload tracking entries that pointed at removed renderings
    // - rebuild the keep-set from current var references the same way
    // ensureImageInputsUploaded constructs upload keys.
    QSet<QString> stillUsed;
    for (const core::WorkflowFile& wf : m_workflowManager.files()) {
        for (const core::WorkflowVar& v : wf.vars) {
            if (v.type != core::WorkflowVarType::Image) continue;
            if (v.imageUuid.isEmpty()) continue;
            stillUsed.insert(v.imageUuid + ":" + v.imageEdits.hash());
        }
    }
    QSet<QString> pruned;
    for (const QString& key : m_uploadedThisSession)
        if (stillUsed.contains(key)) pruned.insert(key);
    m_uploadedThisSession = pruned;

    if (m_statusBar) {
        m_statusBar->showMessage(QString("Cleared unused inputs: %1 images, %2 masks, %3 renders")
                                     .arg(imagesRemoved)
                                     .arg(masksRemoved)
                                     .arg(rendersRemoved));
    }
}

void AppMainWindow::keyPressEvent(QKeyEvent* event)
{
    // Esc only fires here if no focused child consumed it first (popups,
    // dropdowns, dialogs, line-edit IME, etc.) - so this exits fullscreen
    // without stealing Esc from any of them.
    if (event->key() == Qt::Key_Escape && isFullScreen()) {
        auto* anim = utils::propertyAnimate(this, "windowOpacity", windowOpacity(), 0.0, 200,
                                            QEasingCurve::InOutSine);
        connect(anim, &QPropertyAnimation::finished, this, [this]() {
            showNormal();
            utils::propertyAnimate(this, "windowOpacity", windowOpacity(), 1.0, 200,
                                   QEasingCurve::InOutSine);
        });
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void AppMainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() != QEvent::WindowStateChange) return;
    auto* e = static_cast<QWindowStateChangeEvent*>(event);
    const bool wasMinimized = (e->oldState() & Qt::WindowMinimized);
    const bool isMinimized = (windowState() & Qt::WindowMinimized);
    if (wasMinimized && !isMinimized && windowOpacity() < 0.99) {
        utils::propertyAnimate(this, "windowOpacity", windowOpacity(), 1.0, 200,
                               QEasingCurve::InOutSine);
    }

    if (m_chrome) m_chrome->onWindowStateChanged();
}

void AppMainWindow::closeEvent(QCloseEvent* event)
{
    static bool isClosing{false};
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

    // The collector watcher polls a folder on a QTimer; if we let the app
    // tear down without stopping it, an extra tick can fire on a half-
    // destroyed widget. Stop it now while everything's still alive.
    if (m_datasetHelpersPage) {
        if (auto* cp = m_datasetHelpersPage->collectorPage()) cp->stopWatcher();
    }

    // Fade other top-level windows (preview popout, any open dialog) out in
    // parallel with the main window. Same 500 ms duration so they all reach
    // opacity 0 at the same moment - without this, the popout sits at full
    // opacity through the whole main-window fade and snap-hides at the end.
    for (QWidget* w : qApp->topLevelWidgets()) {
        if (w != this && w->isWindow() && w->isVisible()) {
            propertyAnimate(w, "windowOpacity", w->windowOpacity(), 0.0, 500,
                            QEasingCurve::InOutSine);
        }
    }

    connect(propertyAnimate(this, "windowOpacity", 1.0, 0.0, 500, QEasingCurve::InOutSine),
            &QPropertyAnimation::finished, this, [this]() {
                // Hide main + force-close any other top-level windows (popout)
                // so they don't linger on the taskbar past the fade. They've
                // already faded to 0 in parallel above, so hide() is invisible.
                hide();
                for (QWidget* w : qApp->topLevelWidgets()) {
                    if (w != this && w->isWindow()) {
                        w->setAttribute(Qt::WA_DeleteOnClose, false);
                        w->hide();
                        w->deleteLater();
                    }
                }
                qApp->quit();
            });
}

} // namespace gui
