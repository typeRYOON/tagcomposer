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
#include <gui/prompthistorypage.h>
#include <core/prompthistory.h>
#include <gui/dataset/tageditorpage.h>
#include <gui/dataset/collectorpage.h>
#include <core/updatechecker.h>
#include <QDateTime>
#include <QUuid>
#include <gui/widgets/danmakuoverlay.h>
#include <gui/chromeddialog.h>
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
#include <QRegularExpression>
#include <QShortcut>
#include <QtConcurrent>

using namespace utils;

namespace gui {

AppMainWindow::AppMainWindow(QWidget* parent)
    : QMainWindow{parent}, m_entryModel{new core::EntryModel(this)}
{
    m_soundPlayer = new core::SoundPlayer(this);

    setWindowTitle("Tag Composer");
    setWindowOpacity(0.0);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinimizeButtonHint |
                   Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);
    resize(1550, 872);
    setMinimumSize(1420, 920);

    // ---- Load settings
    m_settings = AppSettings::load(BASE_PATH + "/" + SETTINGS_PATH);
    m_soundPlayer->setVolume(m_settings.sfxVolume);

    m_lastComfyEnabled = m_settings.comfyUiEnabled;
    m_lastComfyHost = m_settings.comfyUiServerAddress;

    // ---- ComfyUI client
    m_comfyClient = new core::ComfyUiClient(this);
    m_comfyClient->setServerAddress(m_settings.comfyUiServerAddress);
    m_comfyClient->setApiKey(m_settings.comfyUiApiKey);

    // ---- Workflow input cache
    m_inputCache = new core::WorkflowInputCache(BASE_PATH + "/" + WORKFLOW_INPUTS_DIR, this);
    // Reset upload state on (re)connect so a server restart re-uploads inputs.
    connect(m_comfyClient, &core::ComfyUiClient::connected, this,
            [this]() { m_uploadedThisSession.clear(); });
    connect(m_comfyClient, &core::ComfyUiClient::disconnected, this,
            [this]() { m_uploadedThisSession.clear(); });

    // ---- Load pipeline data
    m_facetIndex = core::FacetIndex::loadFromFile(BASE_PATH + "/" + FACETS_PATH);
    m_facetIndex.loadDefinitionsFromFile(BASE_PATH + "/" + DEFINITIONS_PATH);
    m_ruleEngine = core::RuleEngine::loadFromFile(BASE_PATH + "/" + RULES_PATH);
    m_tagGroupIndex = core::TagGroupIndex::loadFromFile(BASE_PATH + "/" + GROUPS_PATH);
    m_varIndex = core::VariableIndex::loadFromFile(BASE_PATH + "/" + VARS_PATH);
    m_workflowManager = core::WorkflowManager::loadFromFile(BASE_PATH + "/" + WORKFLOWS_PATH);
    m_pipeline = new core::PromptPipeline(&m_facetIndex, &m_ruleEngine, &m_varIndex, this);

    // ---- Pages
    m_tileViewPage = new TileViewPage(m_entryModel, this);
    m_tileViewPage->setLoraDirs(m_settings.loraBaseDir, m_settings.loraTestDir);
    m_tileViewPage->setLoraDefaults(m_settings.defaultLoraModelStr, m_settings.defaultLoraClipStr);
    m_tileViewPage->setComfyClient(m_comfyClient);
    m_tileViewPage->setFacetIndex(&m_facetIndex);
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

    m_promptHistory = new core::PromptHistory(this);
    m_promptHistoryPage = new PromptHistoryPage(m_promptHistory, m_entryModel, m_composerPage,
                                                m_comfyClient, this);

    m_pages = new QStackedWidget(this);
    m_pages->setObjectName("MainPages");
    m_pages->installEventFilter(this);
    // Order must match gui::Page enum and the navbar's addButton sequence.
    m_homePage = new HomePage(this);
    m_pages->addWidget(m_homePage);            // Page::Home
    m_pages->addWidget(m_tileViewPage);        // Page::EntryViewer
    m_pages->addWidget(m_composerPage);        // Page::TagComposer
    m_pages->addWidget(m_workflowEditPage);    // Page::WorkflowEditor
    m_pages->addWidget(m_facetEditorPage);     // Page::FacetEditor
    m_pages->addWidget(m_promptHistoryPage);   // Page::PromptHistory
    m_pages->addWidget(m_outputViewerPage);    // Page::OutputViewer
    m_taggerLibrary = std::make_unique<core::AutoTaggerLibrary>(BASE_PATH + "/" + MODELS_DIR);
    if (m_settings.activeAutoTagModel.isEmpty()) {
        const auto names = m_taggerLibrary->availableModels();
        if (!names.isEmpty()) m_settings.activeAutoTagModel = names.first();
    }

    m_datasetHelpersPage =
        new DatasetHelpersPage(&m_facetIndex, m_taggerLibrary.get(), &m_settings, this);
    m_pages->addWidget(m_datasetHelpersPage); // Page::DatasetHelpers
    m_pages->addWidget(m_wikiPage);           // Page::DanbooruWiki
    m_pages->addWidget(m_settingsPage);       // Page::Settings

    // ---- Danmaku overlay (behind all pages)
    m_danmakuOverlay = new DanmakuOverlay(m_pages);

    NavBar* nav = new NavBar(this, this);
    connect(nav, &NavBar::pageRequested, m_pages, &QStackedWidget::setCurrentIndex);
    connect(m_pages, &QStackedWidget::currentChanged, nav, &NavBar::setCurrentPage);

    // ---- Prompt history page wiring
    connect(m_promptHistoryPage, &PromptHistoryPage::statusMessageRequested, this,
            [this](const QString& msg) { m_statusBar->showMessage(msg); });
    connect(m_promptHistoryPage, &PromptHistoryPage::switchToComposerRequested, this,
            [this]() { m_pages->setCurrentIndex(int(Page::TagComposer)); });
    connect(m_promptHistoryPage, &PromptHistoryPage::openEntryRequested, this,
            [this](int entryId) {
                m_pages->setCurrentIndex(int(Page::EntryViewer));
                m_tileViewPage->clearSearchAndSelect(int32_t(entryId));
            });

    // ---- LoRA stack: tile view -> main window + composer + workflow editor
    connect(m_tileViewPage, &TileViewPage::loraStackChanged, this,
            [this](const QList<core::LoraConfig>& stack) {
                m_activeLoraStack = stack;
                m_activeLoraUuids = m_tileViewPage->activeLoraUuids();
                m_composerPage->setActiveLoraUuids(m_activeLoraUuids);
                m_workflowEditPage->setActiveLoraStack(stack);
            });

    // Mirror workflow-editor strength edits into the cached stack so the next
    // run uses them without waiting for the tile-view to re-emit.
    connect(m_workflowEditPage, &WorkflowEditPage::loraStrengthsChanged, this,
            [this](const QList<core::LoraConfig>& stack) { m_activeLoraStack = stack; });

    // ---- Batch
    connect(m_workflowEditPage, &WorkflowEditPage::batchRunRequested, this,
            &AppMainWindow::runBatch);

    // ---- LoRA restore: composer session/state -> tile view
    connect(m_composerPage, &PromptComposerPage::loraUuidsRestored, m_tileViewPage,
            &TileViewPage::setLoraActiveByUuids);

    // ---- Wire export: extraBtn -> composer page
    connect(m_tileViewPage, &TileViewPage::tagsExported, m_composerPage,
            &PromptComposerPage::loadPipeline);
    connect(m_tileViewPage, &TileViewPage::entryTagAdded, m_composerPage,
            &PromptComposerPage::onEntryTagAdded);
    connect(m_tileViewPage, &TileViewPage::entryTagRemoved, m_composerPage,
            &PromptComposerPage::onEntryTagRemoved);

    // ---- Entry deletion: drop pushes/lora referencing it from composer
    connect(m_entryModel, &core::EntryModel::entryDeleted, m_composerPage,
            &PromptComposerPage::onEntryDeleted);

    // ---- Image-slot removal: drop the push for that slot and reindex
    // higher slots so the composer mirrors the model's reindexing.
    connect(m_entryModel, &core::EntryModel::imageRemovedFromEntry, m_composerPage,
            &PromptComposerPage::onImageRemoved);

    // ---- Sync push-group state back to tile view
    connect(m_composerPage, &PromptComposerPage::activeGroupsChanged, m_tileViewPage,
            &TileViewPage::setActiveGroups);

    // ---- Wire facet editor reload
    connect(m_facetEditorPage, &FacetEditorPage::facetsDefined, this, &AppMainWindow::reloadFacets);

    // Last "undefined in composer" tag was just defined - jump back to composer.
    connect(m_facetEditorPage, &FacetEditorPage::composerRequested, this, [this]() {
        m_pages->setCurrentIndex(int(Page::TagComposer));
    });

    // Re-read facets.fct, keep tag definitions intact.
    connect(m_facetEditorPage, &FacetEditorPage::schemaReloadRequested, this, [this]() {
        m_facetIndex.reloadSchemaFromFile(BASE_PATH + "/" + FACETS_PATH);
        reloadFacets();
        m_statusBar->showMessage("Facet schema reloaded.");
    });

    // ---- Quick-add facet shortcut
    // Panels emit (tag, facetName); FacetIndex lives here, so apply + persist.
    connect(m_composerPage, &PromptComposerPage::quickFacetRequested, this,
            &AppMainWindow::applyQuickFacet);
    connect(m_tileViewPage, &TileViewPage::quickFacetRequested, this,
            &AppMainWindow::applyQuickFacet);
    if (auto* tcp = m_datasetHelpersPage->tagClusterPage()) {
        connect(tcp, &TagClusterPage::quickFacetRequested, this, &AppMainWindow::applyQuickFacet);
        tcp->setQuickFacets(m_settings.quickCharacterFacet, m_settings.quickCopyrightFacet,
                            m_settings.quickTriggerWordFacet, m_settings.quickStyleFacet);

        connect(tcp, &TagClusterPage::createEntryRequested, this,
                [this](const QString& title, const QStringList& tags) {
                    if (!m_entryModel) return;
                    core::Entry entry;
                    entry.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    entry.title = title.trimmed();
                    entry.creationTime = QDateTime::currentSecsSinceEpoch();
                    // Single placeholder image slot; user can drop a real
                    // file later. Tags are seeded from the cluster results.
                    core::ImageData img;
                    img.fileName = "00001.png";
                    img.tagIds = m_entryModel->getTagIds(tags);
                    entry.images.append(std::move(img));

                    const QString newUuid = entry.uuid;
                    m_entryModel->addEntry(std::move(entry));
                    m_tileViewPage->refreshEntries();
                    m_pages->setCurrentIndex(int(Page::EntryViewer));
                    if (core::Entry* fresh = m_entryModel->entryByUuid(newUuid))
                        m_tileViewPage->selectEntry(fresh->id);
                    if (m_statusBar)
                        m_statusBar->showMessage(QString("Created entry \"%1\" with %2 tag(s).")
                                                     .arg(title)
                                                     .arg(tags.size()));
                });
    }

    // ---- Wiki page navigation
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

    // ---- Facet editor navigation
    auto showFacetEditor = [this](const QString& tag) {
        m_pages->setCurrentIndex(int(Page::FacetEditor));
        m_facetEditorPage->selectTagByName(tag);
    };
    connect(m_tileViewPage, &TileViewPage::facetEditorRequested, this, showFacetEditor);
    connect(m_composerPage, &PromptComposerPage::facetEditorRequested, this, showFacetEditor);
    if (auto* tcp = m_datasetHelpersPage->tagClusterPage())
        connect(tcp, &TagClusterPage::facetEditorRequested, this, showFacetEditor);

    // ---- Workflow editor navigation
    connect(m_composerPage, &PromptComposerPage::workflowEditorRequested, this, [this]() {
        m_workflowEditPage->refresh();
        m_pages->setCurrentIndex(int(Page::WorkflowEditor));
    });
    connect(m_composerPage, &PromptComposerPage::workflowVarsChanged, m_workflowEditPage,
            &WorkflowEditPage::refresh);

    // ---- Run with workflow
    // Wildcard vars pick fresh tags per run, so the prompt is rebuilt each
    // iteration to flow that pick through the rule/var pipeline.
    connect(m_composerPage, &PromptComposerPage::runRequested, this, [this](int count) {
        const core::WorkflowFile* wf = m_workflowManager.selectedFile();
        if (!wf) return;

        const QStringList missing = unloadedImageInputs();
        if (!missing.isEmpty()) {
            m_statusBar->showMessage(
                QString("Run blocked - image input(s) not loaded: %1").arg(missing.join(", ")));
            return;
        }

        QFile f(wf->absolutePath());
        if (!f.open(QIODevice::ReadOnly)) return;
        const QString tmpl = QString::fromUtf8(f.readAll());

        const QStringList issues =
            workflowTemplateIssues(tmpl, m_activeLoraStack.size());
        if (!issues.isEmpty()) {
            m_statusBar->showMessage(QString("Run blocked - %1").arg(issues.join("; ")));
            return;
        }

        ensureImageInputsUploaded([this, tmpl, count]() {
            QList<core::LoraConfig> healed = m_activeLoraStack;
            healLoraStackInPlace(healed);

            for (int i = 0; i < count; ++i) {
                const QStringList wildTags = m_workflowManager.pickWildcardTags();
                const QString positivePrompt =
                    wildTags.isEmpty() ? m_composerPage->currentPromptString(true)
                                       : m_composerPage->computePromptWithExtraTags(wildTags, true);

                QString json = m_workflowManager.applyToJson(tmpl);
                core::WorkflowManager::applyPositive(json, positivePrompt);
                core::WorkflowManager::applyLoraStack(json, healed);
                recordAndQueue(json, positivePrompt, healed);
            }
            m_workflowManager.saveToFile(BASE_PATH + "/" + WORKFLOWS_PATH);
            m_workflowEditPage->refresh();
        });
    });

    // ---- Shift+cancel = clear pending queue
    connect(m_composerPage, &PromptComposerPage::clearPendingRequested, this, [this]() {
        m_skipFinalOnPendingClear = true;
        m_comfyClient->clearPending();
    });

    // ---- Settings page
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
        m_tileViewPage->setLoraDirs(m_settings.loraBaseDir, m_settings.loraTestDir);
        m_tileViewPage->setLoraDefaults(m_settings.defaultLoraModelStr,
                                        m_settings.defaultLoraClipStr);
    });
    connect(m_settingsPage, &SettingsPage::reconnectRequested, this,
            [this]() { m_comfyClient->connectToServer(); });

    connect(m_settingsPage, &SettingsPage::exportEntriesRequested, this, [this]() {
        ExportDialog dlg(m_entryModel, &m_facetIndex, m_danbooruIndex, this);
        dlg.exec();
    });
    connect(m_settingsPage, &SettingsPage::importEntriesRequested, this, [this]() {
        ImportDialog dlg(m_entryModel, &m_facetIndex, BASE_PATH + "/data/entry",
                         BASE_PATH + "/" + DEFINITIONS_PATH, BASE_PATH + "/" + FACETS_PATH, this);
        if (dlg.exec() == QDialog::Accepted) {
            m_tileViewPage->refreshEntries();
            reloadFacets();
        }
    });

    connect(m_settingsPage, &SettingsPage::clearUnusedInputsRequested, this,
            &AppMainWindow::clearUnusedInputs);

    connect(m_settingsPage, &SettingsPage::purgeTagDefinitionsRequested, this,
            &AppMainWindow::purgeTagDefinitions);
    connect(m_settingsPage, &SettingsPage::purgeUnknownFacetsRequested, this,
            &AppMainWindow::purgeUnknownFacets);

    connect(m_comfyClient, &core::ComfyUiClient::connected, this,
            [this]() { m_settingsPage->setComfyStatus(true); });
    connect(m_comfyClient, &core::ComfyUiClient::disconnected, this,
            [this]() { m_settingsPage->setComfyStatus(false); });
    connect(m_comfyClient, &core::ComfyUiClient::connectionError, this,
            [this](const QString& err) { m_settingsPage->setComfyStatus(false, err); });
    connect(m_comfyClient, &core::ComfyUiClient::previewImageReady, m_composerPage,
            &PromptComposerPage::setPreviewImage);
    connect(m_comfyClient, &core::ComfyUiClient::queueCountChanged, this, [this](int count) {
        // Lambda receiver: m_statusBar is constructed below this connect.
        m_statusBar->setActiveCount(count);
        m_composerPage->setComfyActiveCount(count);

        const bool jobFinished = (count < m_lastQueueCount);
        m_lastQueueCount = count;

        // Each queue decrement is a finished prompt. Load its output unless
        // we just interrupted / cleared (in which case "newest temp image"
        // is stale). Delay gives ComfyUI time to write the file.
        if (jobFinished && m_skipNextFinalLoad) {
            m_skipNextFinalLoad = false;
            m_pendingFinalLoad = false;
            return;
        }
        if (jobFinished && m_skipFinalOnPendingClear) {
            m_skipFinalOnPendingClear = false;
            return;
        }
        if (jobFinished && m_pendingFinalLoad) {
            m_pendingFinalLoad = false;
            QTimer::singleShot(500, this, [this]() { loadFinalPreview(); });
        }
    });
    connect(m_comfyClient, &core::ComfyUiClient::previewProgressChanged, this,
            [this](int step, int total) {
                m_statusBar->setProgress(step, total);
                m_composerPage->setComfyProgress(step, total);
                if (step > 0 && total > 0) m_pendingFinalLoad = true;
            });
    connect(m_composerPage, &PromptComposerPage::interruptRequested, this, [this]() {
        m_skipNextFinalLoad = true;
        m_comfyClient->interrupt();
    });

    // ---- Layout
    QWidget* content = new QWidget(this);
    auto* hLayout = new QHBoxLayout(content);
    hLayout->setContentsMargins(0, 0, 0, 0);
    hLayout->setSpacing(0);
    hLayout->addWidget(nav);
    hLayout->addWidget(m_pages, 1);

    m_statusBar = new StatusBar(this);

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

    // ---- Background: load DanbooruIndex
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

    // app.qss sorts first so its app-wide rules act as the base.
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

    // ---- Global keyboard shortcuts
    // Guard against firing while typing in a text input.
    auto textInput = []() -> bool {
        const QWidget* fw = qApp->focusWidget();
        return fw &&
               (qobject_cast<const QLineEdit*>(fw) || qobject_cast<const QPlainTextEdit*>(fw));
    };
    // WindowShortcut so child windows (preview popout) can re-bind the keys.
    auto sc = [this](QKeySequence key, auto fn) {
        auto* s = new QShortcut(key, this);
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

    // ---- Update check
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

    // API key rides on each REST request via extra_data, not the WS handshake,
    // so a key change doesn't need a reconnect.
    const bool enabledChanged = (m_settings.comfyUiEnabled != m_lastComfyEnabled);
    const bool hostChanged = (m_settings.comfyUiServerAddress != m_lastComfyHost);
    const bool needsBounce = enabledChanged || hostChanged;

    m_lastComfyEnabled = m_settings.comfyUiEnabled;
    m_lastComfyHost = m_settings.comfyUiServerAddress;

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
    m_facetIndex.saveDefinitions(BASE_PATH + "/" + DEFINITIONS_PATH);
    m_facetEditorPage->reload();
    m_composerPage->repush();
    m_tileViewPage->refreshTags();
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
    reloadFacets();
    m_statusBar->showMessage(QString("Added facet '%1' to '%2'").arg(facetName, tag));
}

// ---- Batch run

void AppMainWindow::runBatch(const QString& query)
{
    const core::WorkflowFile* wf = m_workflowManager.selectedFile();
    if (!wf) {
        m_statusBar->showMessage("Batch: no workflow selected");
        return;
    }

    const QStringList missing = unloadedImageInputs();
    if (!missing.isEmpty()) {
        m_statusBar->showMessage(
            QString("Batch blocked - image input(s) not loaded: %1").arg(missing.join(", ")));
        return;
    }

    const QList<core::Entry*> matched = m_entryModel->filter(query);
    if (matched.isEmpty()) {
        m_statusBar->showMessage(QString("Batch: query \"%1\" matched 0 entries").arg(query));
        return;
    }

    QFile f(wf->absolutePath());
    if (!f.open(QIODevice::ReadOnly)) {
        m_statusBar->showMessage("Batch: workflow file unreadable");
        return;
    }
    const QString tmpl = QString::fromUtf8(f.readAll());

    // Worst-case LoRA count across the matched set: any entry with its own
    // lora bumps the effective stack by one, so the template needs that
    // many slots to avoid silent drops.
    int maxLoraCount = m_activeLoraStack.size();
    for (core::Entry* entry : matched) {
        if (entry->lora.has_value()) {
            maxLoraCount = m_activeLoraStack.size() + 1;
            break;
        }
    }
    const QStringList issues = workflowTemplateIssues(tmpl, maxLoraCount);
    if (!issues.isEmpty()) {
        m_statusBar->showMessage(QString("Batch blocked - %1").arg(issues.join("; ")));
        return;
    }

    const int count = m_composerPage->currentPromptCount();

    ensureImageInputsUploaded([this, matched, tmpl, count]() {
        int dispatched = 0;
        int skipped = 0;

        for (core::Entry* entry : matched) {
            QList<QString> tags;
            if (!entry->images.isEmpty()) tags = m_entryModel->getTags(entry->images[0].tagIds);

            // No tags = same prompt as composer state alone, not batchable.
            if (tags.isEmpty()) {
                ++skipped;
                continue;
            }

            QList<core::LoraConfig> stackForEntry = m_activeLoraStack;
            if (entry->lora.has_value()) stackForEntry << entry->lora.value();
            healLoraStackInPlace(stackForEntry);

            for (int i = 0; i < count; ++i) {
                QList<QString> extraTags = tags;
                for (const QString& wt : m_workflowManager.pickWildcardTags())
                    extraTags << wt;
                const QString positivePrompt =
                    m_composerPage->computePromptWithExtraTags(extraTags, true);

                QString json = m_workflowManager.applyToJson(tmpl);
                core::WorkflowManager::applyPositive(json, positivePrompt);
                core::WorkflowManager::applyLoraStack(json, stackForEntry);
                recordAndQueue(json, positivePrompt, stackForEntry, int(entry->id));
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

    // Newest by mtime: the run that just finished is the freshest write.
    const QFileInfo* newest = &files[0];
    for (const QFileInfo& fi : files)
        if (fi.lastModified() > newest->lastModified()) newest = &fi;

    QImage img(newest->absoluteFilePath());
    if (!img.isNull()) m_composerPage->setPreviewImage(img);
}

QStringList AppMainWindow::unloadedImageInputs() const
{
    QStringList missing;
    if (!m_workflowManager.selectedFile()) return missing;
    for (const auto& var : m_workflowManager.variables()) {
        if (var.type != core::WorkflowVarType::Image) continue;
        if (var.imageUuid.isEmpty()) missing << var.placeholder;
    }
    return missing;
}

QStringList AppMainWindow::workflowTemplateIssues(const QString& tmpl,
                                                  int activeLoraCount) const
{
    QStringList issues;
    constexpr int kLoraSlots = 10; // matches applyLoraStack's default maxSlots

    // 1. Variables declared on the workflow whose placeholder/token never
    // shows up in the raw template. Wildcards fold into __positive__ and
    // have no direct token, so skip them.
    QStringList unusedVars;
    for (const core::WorkflowVar& var : m_workflowManager.variables()) {
        if (var.type == core::WorkflowVarType::Wildcard) continue;
        if (var.type == core::WorkflowVarType::LatentSize) {
            const bool wEmpty = var.latentWidthToken.isEmpty();
            const bool hEmpty = var.latentHeightToken.isEmpty();
            if (wEmpty && hEmpty) {
                unusedVars << QStringLiteral("(latent size: no tokens)");
                continue;
            }
            if (!wEmpty && !tmpl.contains(var.latentWidthToken))
                unusedVars << var.latentWidthToken;
            if (!hEmpty && !tmpl.contains(var.latentHeightToken))
                unusedVars << var.latentHeightToken;
            continue;
        }
        if (var.placeholder.isEmpty()) {
            unusedVars << QString("(unnamed %1 var)")
                              .arg(core::WorkflowManager::typeToStr(var.type));
            continue;
        }
        if (!tmpl.contains(var.placeholder)) unusedVars << var.placeholder;
    }
    if (!unusedVars.isEmpty())
        issues << QString("unused variable(s): %1").arg(unusedVars.join(", "));

    // 2. __dunder__ tokens in the template that no handler will touch.
    // Built-ins like __positive__ are intentionally optional (e.g. upscale
    // workflows drop the prompt), and applyLoraStack always wipes every
    // __lora_*_N__ for N=1..maxSlots (empty slots get "None"), so all of
    // those are pre-considered handled regardless of presence.
    QSet<QString> handled{QStringLiteral("__positive__"),
                          QStringLiteral("__lora_count__")};
    for (int slot = 1; slot <= kLoraSlots; ++slot) {
        handled.insert(QString("__lora_name_%1__").arg(slot));
        handled.insert(QString("__lora_wt_%1__").arg(slot));
        handled.insert(QString("__lora_model_str_%1__").arg(slot));
        handled.insert(QString("__lora_clip_str_%1__").arg(slot));
    }
    for (const core::WorkflowVar& var : m_workflowManager.variables()) {
        if (var.type == core::WorkflowVarType::Wildcard) continue;
        if (var.type == core::WorkflowVarType::LatentSize) {
            if (!var.latentWidthToken.isEmpty()) handled.insert(var.latentWidthToken);
            if (!var.latentHeightToken.isEmpty()) handled.insert(var.latentHeightToken);
            continue;
        }
        if (!var.placeholder.isEmpty()) handled.insert(var.placeholder);
    }

    // Non-greedy so "__a__b__" yields "__a__", not the whole span. Inner
    // chars can include underscores but not the bracketing "__".
    static const QRegularExpression dunderRe(QStringLiteral("__[A-Za-z0-9_]+?__"));
    QSet<QString> stray;
    auto it = dunderRe.globalMatch(tmpl);
    while (it.hasNext()) {
        const QString tok = it.next().captured(0);
        if (!handled.contains(tok)) stray.insert(tok);
    }
    if (!stray.isEmpty()) {
        QStringList list(stray.cbegin(), stray.cend());
        list.sort();
        issues << QString("unresolved token(s): %1").arg(list.join(", "));
    }

    // 3. LoRA coverage: if N LoRAs are active, slots __lora_name_1__ ..
    // __lora_name_N__ must all be in the template, otherwise applyLoraStack
    // has nowhere to write those LoRAs and they're silently dropped. With
    // no LoRAs active the template is free to omit lora tokens entirely.
    if (activeLoraCount > 0) {
        QStringList missingSlots;
        const int slotsNeeded = std::min(activeLoraCount, kLoraSlots);
        for (int slot = 1; slot <= slotsNeeded; ++slot) {
            const QString tok = QString("__lora_name_%1__").arg(slot);
            if (!tmpl.contains(tok)) missingSlots << tok;
        }
        if (!missingSlots.isEmpty())
            issues << QString("%1 LoRA(s) active but missing slot(s): %2")
                          .arg(activeLoraCount)
                          .arg(missingSlots.join(", "));
    }

    return issues;
}

void AppMainWindow::recordAndQueue(const QString& renderedJson, const QString& positivePrompt,
                                   const QList<core::LoraConfig>& healed, int batchEntryId)
{
    if (m_promptHistory) {
        core::PromptRecord rec;
        rec.queuedAt = QDateTime::currentDateTime();
        if (const core::WorkflowFile* wf = m_workflowManager.selectedFile()) rec.workflowName = wf->name;
        rec.positivePrompt = positivePrompt;
        rec.renderedJson = renderedJson;
        rec.snapshot = m_composerPage->currentSnapshot();
        rec.lorasUsed = healed;
        rec.batchEntryId = batchEntryId;

        // Batch iterations don't go through the composer's push system, so the
        // iterated entry is absent from snapshot.activePushes AND its tags
        // are absent from snapshot.activeTags (the batch unions them only
        // transiently inside computePromptWithExtraTags). Synthesize the push
        // and merge the tags so the history reflects the effective state that
        // produced the prompt -- "Active entries" lists the entry, the row
        // count matches reality, and restore/save-state replay the actual tag
        // set instead of just the pre-batch composer state.
        if (batchEntryId >= 0 && m_entryModel) {
            if (core::Entry* e = m_entryModel->entryById(batchEntryId)) {
                if (!e->images.isEmpty()) {
                    core::EntryPush push;
                    push.uuid = e->uuid;
                    push.imageFileName = e->images[0].fileName;
                    push.tags = m_entryModel->getTags(e->images[0].tagIds);
                    rec.snapshot.activePushes.prepend(push);

                    // Union tags into activeTags, preserving order and skipping
                    // duplicates already in the composer state.
                    QSet<QString> seen(rec.snapshot.activeTags.cbegin(),
                                       rec.snapshot.activeTags.cend());
                    for (const QString& t : push.tags) {
                        if (!seen.contains(t)) {
                            rec.snapshot.activeTags << t;
                            seen.insert(t);
                        }
                    }
                }
            }
        }

        m_promptHistory->append(std::move(rec));
    }
    m_comfyClient->queuePrompt(renderedJson);
}

// Tracking key is (uuid + editsHash) so editing forces a re-upload.
void AppMainWindow::ensureImageInputsUploaded(std::function<void()> done)
{
    struct UploadTask {
        QString trackingKey;
        QString localPath; // may be a rendered edited variant
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

void AppMainWindow::healLoraStackInPlace(QList<core::LoraConfig>& stack)
{
    // Mirror healed shape back to the source entry so future runs skip the work.
    for (core::LoraConfig& lc : stack) {
        if (!lc.healAcrossRoots(m_settings.loraBaseDir, m_settings.loraTestDir)) continue;
        if (lc.sha256.isEmpty()) continue;
        if (core::Entry* e = m_entryModel->entryByLoraSha256(lc.sha256)) {
            if (e->lora.has_value()) {
                e->lora->rootKey = lc.rootKey;
                e->lora->file = lc.file;
                m_entryModel->saveEntry(e->id);
            }
        }
    }
}

void AppMainWindow::clearUnusedInputs()
{
    if (!m_inputCache) return;

    QSet<QString> usedImageUuids;
    QSet<QString> usedMaskIds;
    QSet<QString> usedEditsHashes;
    auto recordRefs = [&](const core::WorkflowVar& v) {
        if (v.type != core::WorkflowVarType::Image) return;
        if (!v.imageUuid.isEmpty()) usedImageUuids.insert(v.imageUuid);
        if (!v.imageEdits.maskId.isEmpty()) usedMaskIds.insert(v.imageEdits.maskId);
        if (v.imageEdits.enabled) usedEditsHashes.insert(v.imageEdits.hash());
    };
    for (const core::WorkflowFile& wf : m_workflowManager.files())
        for (const core::WorkflowVar& v : wf.vars) recordRefs(v);
    if (m_composerPage)
        for (const core::WorkflowVar& v : m_composerPage->imageVarsFromStates()) recordRefs(v);

    int imagesRemoved = 0;
    int masksRemoved = 0;
    int rendersRemoved = 0;

    // Snapshot all() first since remove() mutates the map.
    for (const core::WorkflowInput& inp : m_inputCache->all()) {
        if (!usedImageUuids.contains(inp.uuid)) {
            m_inputCache->remove(inp.uuid);
            ++imagesRemoved;
        }
    }

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

    // Edit variants live under _edited/<editsHash>/, drop whole subdirectories.
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
    auto recordUploadKey = [&](const core::WorkflowVar& v) {
        if (v.type != core::WorkflowVarType::Image) return;
        if (v.imageUuid.isEmpty()) return;
        stillUsed.insert(v.imageUuid + ":" + v.imageEdits.hash());
    };
    for (const core::WorkflowFile& wf : m_workflowManager.files())
        for (const core::WorkflowVar& v : wf.vars) recordUploadKey(v);
    if (m_composerPage)
        for (const core::WorkflowVar& v : m_composerPage->imageVarsFromStates())
            recordUploadKey(v);
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

void AppMainWindow::purgeTagDefinitions()
{
    const core::TagIndex& tagIndex = m_entryModel->tagIndex();
    QList<QString> victims;
    for (const QString& tag : m_facetIndex.allDefinedTags()) {
        const bool noFacets = m_facetIndex.facetsFor(tag).isEmpty();
        // Defer Danbooru pruning until the index is ready - otherwise every
        // tag would look "not in Danbooru" while it's still loading.
        const bool inDanbooru = m_danbooruIndex && m_danbooruIndex->tagCategory(tag) >= 0;
        const bool orphaned = m_danbooruIndex && !inDanbooru && !tagIndex.tagInUse(tag);
        if (noFacets || orphaned) victims << tag;
    }

    if (victims.isEmpty()) {
        if (m_statusBar) m_statusBar->showMessage("No stale tag definitions to purge.");
        return;
    }

    const bool confirmed = ChromedDialog::confirm(
        this, "Purge tag definitions",
        QString("Drop %1 tag definition(s) that have no facets, or aren't in the "
                "Danbooru list and aren't used by any entry?\n\nThe change applies "
                "in memory and persists at the next save.")
            .arg(victims.size()),
        "Purge", "Cancel");
    if (!confirmed) return;

    for (const QString& tag : victims) m_facetIndex.setDefinition(tag, {});
    reloadFacets();

    if (m_statusBar)
        m_statusBar->showMessage(QString("Purged %1 tag definition(s).").arg(victims.size()));
}

void AppMainWindow::purgeUnknownFacets()
{
    const QSet<QString> known(m_facetIndex.allFacets().cbegin(), m_facetIndex.allFacets().cend());

    QHash<QString, QList<QString>> survivors;     // tag -> facet list after stripping
    QSet<QString> offenders;                      // distinct unknown facet names
    int strippedFacets = 0;
    int affectedTags = 0;
    for (const QString& tag : m_facetIndex.allDefinedTags()) {
        const QList<QString> current = m_facetIndex.facetsFor(tag);
        QList<QString> kept;
        kept.reserve(current.size());
        for (const QString& f : current) {
            if (known.contains(f))
                kept << f;
            else {
                offenders.insert(f);
                ++strippedFacets;
            }
        }
        if (kept.size() != current.size()) {
            survivors.insert(tag, kept);
            ++affectedTags;
        }
    }

    if (strippedFacets == 0) {
        if (m_statusBar) m_statusBar->showMessage("No unknown facets to purge.");
        return;
    }

    QStringList offenderList(offenders.cbegin(), offenders.cend());
    std::sort(offenderList.begin(), offenderList.end());
    const int previewCap = 12;
    QString preview = offenderList.mid(0, previewCap).join(", ");
    if (offenderList.size() > previewCap)
        preview += QString(", +%1 more").arg(offenderList.size() - previewCap);

    const bool confirmed = ChromedDialog::confirm(
        this, "Purge unknown facets",
        QString("Strip %1 facet entr%2 (%3 distinct name(s): %4) from %5 tag definition(s)?\n\n"
                "The change applies in memory and persists at the next save.")
            .arg(strippedFacets)
            .arg(strippedFacets == 1 ? "y" : "ies")
            .arg(offenderList.size())
            .arg(preview)
            .arg(affectedTags),
        "Purge", "Cancel");
    if (!confirmed) return;

    for (auto it = survivors.cbegin(); it != survivors.cend(); ++it)
        m_facetIndex.setDefinition(it.key(), it.value());
    reloadFacets();

    if (m_statusBar)
        m_statusBar->showMessage(QString("Stripped %1 unknown facet entr%2 from %3 tag(s).")
                                     .arg(strippedFacets)
                                     .arg(strippedFacets == 1 ? "y" : "ies")
                                     .arg(affectedTags));
}

void AppMainWindow::keyPressEvent(QKeyEvent* event)
{
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

    if (m_datasetHelpersPage) {
        if (auto* cp = m_datasetHelpersPage->collectorPage()) cp->stopWatcher();
    }

    for (QWidget* w : qApp->topLevelWidgets()) {
        if (w != this && w->isWindow() && w->isVisible()) {
            propertyAnimate(w, "windowOpacity", w->windowOpacity(), 0.0, 500,
                            QEasingCurve::InOutSine);
        }
    }

    connect(propertyAnimate(this, "windowOpacity", 1.0, 0.0, 500, QEasingCurve::InOutSine),
            &QPropertyAnimation::finished, this, [this]() {
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
