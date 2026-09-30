#include <app/app_window.h>
#include <app/comfy_client.h>
#include <app/composer_page.h>
#include <app/facet_editor_page.h>
#include <app/settings_page.h>
#include <app/output_viewer_page.h>
#include <app/prompt_history_page.h>
#include <app/tag_wiki_page.h>
#include <core/prompt_history.h>
#include <app/workflow_edit_page.h>
#include <app/workflow_input_cache.h>
#include <app/dataset_helpers_page.h>
#include <tagger/tagger_library.h>
#include <app/entry_viewer_page.h>
#include <app/tag_cluster_page.h>
#include <app/home_page.h>
#include <app/nav_bar.h>
#include <app/page.h>
#include <app/paths.h>
#include <app/status_bar.h>
#include <app/window_chrome.h>
#include <app/widget_utils.h>
#include <QApplication>
#include <QCloseEvent>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPropertyAnimation>
#include <QShortcut>
#include <QWindowStateChangeEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QShowEvent>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

// The queue count drops before the save node has written the file.
constexpr int kFinalLoadDelayMs = 500;

constexpr auto kDiscordUrl = "https://discord.gg/4jgC8C9Ku8";

QWidget* placeholder(const QString& title)
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);

    auto* label = new QLabel(title);
    label->setAlignment(Qt::AlignCenter);
    label->setEnabled(false);
    layout->addWidget(label);

    return page;
}

} // namespace

AppWindow::AppWindow(const QString& dataDir, QWidget* parent)
    : QMainWindow(parent), m_dataDir(dataDir)
{
    // Flags before sizing: setWindowFlags recreates the native window and drops
    // its geometry. The button hints keep Windows' snap and minimize animation.
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinimizeButtonHint
                   | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);

    // Sized for the composer page.
    resize(1550, 872);
    setMinimumSize(1420, 920);

    m_chrome = new WindowChrome(this);
    setCentralWidget(m_chrome->frame());

    // Before the pages, which connect to these.
    m_comfy = new ComfyClient(this);
    m_history = new PromptHistory(this);
    m_status = new StatusBar;
    m_inputCache = new WorkflowInputCache(dataDir + u"/"_s
                                              + QString::fromLatin1(paths::kWorkflowInputsDir),
                                          this);
    m_nav = new NavBar(m_chrome->body());

    m_pages = new QStackedWidget;
    m_pages->setObjectName(u"MainPages"_s);
    for (int i = 0; i < kPageCount; ++i)
        m_pages->addWidget(buildPage(i));

    auto* content = new QWidget;
    auto* row = new QHBoxLayout(content);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addWidget(m_nav);
    row->addWidget(m_pages, 1);

    auto* body = new QVBoxLayout(m_chrome->body());
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    body->addWidget(content, 1);
    body->addWidget(m_status);

    connect(m_nav, &NavBar::pageRequested, m_pages, &QStackedWidget::setCurrentIndex);
    connect(m_pages, &QStackedWidget::currentChanged, m_nav, &NavBar::setCurrentPage);
    connect(m_pages, &QStackedWidget::currentChanged, this, &AppWindow::updateTitle);
    connect(m_pages, &QStackedWidget::currentChanged, this, [this](int index) {
        // The composer's selection may have changed while this page was hidden.
        if (m_workflowPage && index == int(Page::WorkflowEditor)) m_workflowPage->refresh();
    });
    connect(m_nav, &NavBar::discordRequested, this,
            []() { QDesktopServices::openUrl(QUrl(QString::fromLatin1(kDiscordUrl))); });

    // Live feed: previews to the composer, progress to the status bar.
    connect(m_comfy, &ComfyClient::previewImageReady, this, [this](const QImage& image) {
        if (m_composerPage) m_composerPage->setPreviewImage(image);
    });
    connect(m_comfy, &ComfyClient::previewProgressChanged, this, [this](int step, int total) {
        m_status->setProgress(step, total);
        if (m_composerPage) m_composerPage->setComfyProgress(step, total);

        // Sampling, so load the result when the queue drops.
        if (step > 0 && total > 0) m_pendingFinalLoad = true;

        // A new run started; cancel the previous run's pending final load.
        if (step <= 1) ++m_sampleGeneration;
    });
    connect(m_comfy, &ComfyClient::queueCountChanged, this, [this](int count) {
        m_status->setActiveCount(count);
        if (m_composerPage) m_composerPage->setComfyActiveCount(count);

        // A shorter queue means a prompt finished; replace the last preview frame
        // with the final image.
        const bool finished = count < m_lastQueueCount;
        m_lastQueueCount = count;
        if (!finished) return;

        if (m_skipNextFinalLoad) {
            m_skipNextFinalLoad = false;
            m_pendingFinalLoad = false;
            return;
        }
        if (m_skipPendingClearLoad) {
            m_skipPendingClearLoad = false;
            return;
        }
        if (!m_pendingFinalLoad) return;

        m_pendingFinalLoad = false;

        const int generation = m_sampleGeneration;
        QTimer::singleShot(kFinalLoadDelayMs, this, [this, generation]() {
            if (generation != m_sampleGeneration) return; // a newer run owns the preview now
            loadFinalPreview();
        });
    });

    // After a cancel the newest temp image is stale; don't load it.
    connect(m_comfy, &ComfyClient::interruptSent, this,
            [this]() { m_skipNextFinalLoad = true; });
    connect(m_comfy, &ComfyClient::pendingCleared, this,
            [this]() { m_skipPendingClearLoad = true; });
    connect(m_comfy, &ComfyClient::connected, this,
            [this]() { if (m_settingsPage) m_settingsPage->setComfyStatus(true); });
    connect(m_comfy, &ComfyClient::disconnected, this,
            [this]() { if (m_settingsPage) m_settingsPage->setComfyStatus(false); });

    connect(&m_data, &AppData::loaded, this, [this]() {
        m_comfy->setServerAddress(m_data.settings.comfyServerAddress);
        m_comfy->setApiKey(m_data.settings.comfyApiKey);
        if (m_data.settings.comfyEnabled) m_comfy->connectToServer();

        if (m_composerPage) {
            m_composerPage->reloadAll();
            // After the entries load, since pushes reference them.
            if (!m_sessionRestored) {
                m_sessionRestored = true;
                m_composerPage->restoreSession(m_data.dataPath(paths::kSession));
            }
        }
        if (m_viewerPage) {
            m_viewerPage->applySettings();
            m_viewerPage->setDanbooruIndex(&m_data.danbooru);
        }
        if (m_datasetPage) {
            m_datasetPage->setDanbooruIndex(&m_data.danbooru);
            m_datasetPage->setQuickFacets(m_data.settings.quickCharacterFacet,
                                          m_data.settings.quickCopyrightFacet,
                                          m_data.settings.quickTriggerWordFacet,
                                          m_data.settings.quickStyleFacet);
        }
        if (m_settingsPage) m_settingsPage->reload();
        if (m_workflowPage) m_workflowPage->refresh();
        if (m_facetPage) m_facetPage->reload();
        if (m_wikiPage) m_wikiPage->setIndex(&m_data.danbooru);
        if (m_outputPage) m_outputPage->setOutputFolder(m_data.settings.comfyOutputFolder);
        m_status->showMessage(m_data.problems().isEmpty()
                                  ? u"%1 entries loaded"_s.arg(m_data.entries.count())
                                  : m_data.problems().join(u"  |  "_s));
    });

    installShortcuts();

    m_pages->setCurrentIndex(int(Page::Home));
    updateTitle();

    // Fade in once the first frame is built.
    setWindowOpacity(0.0);
    QTimer::singleShot(0, this, [this]() {
        propertyAnimate(this, "windowOpacity", 0.0, 1.0, 250, QEasingCurve::InOutSine);
    });
}

AppWindow::~AppWindow() = default;

void AppWindow::loadFinalPreview()
{
    if (!m_composerPage) return;

    const QString folder = m_data.settings.comfyTempFolder;
    if (folder.isEmpty()) return;

    static const QStringList filters = {u"*.png"_s, u"*.jpg"_s, u"*.jpeg"_s, u"*.webp"_s};
    const QFileInfoList files = QDir(folder).entryInfoList(filters, QDir::Files);
    if (files.isEmpty()) return;

    // Newest file wins.
    const QFileInfo* newest = &files[0];
    for (const QFileInfo& info : files)
        if (info.lastModified() > newest->lastModified()) newest = &info;

    const QImage image(newest->absoluteFilePath());
    if (!image.isNull()) m_composerPage->setPreviewImage(image);
}


void AppWindow::closeEvent(QCloseEvent* event)
{
    // First pass saves and fades out; the second accepts.
    if (m_closing) {
        event->accept();
        return;
    }
    event->ignore();
    m_closing = true;

    // Stop the collector before it moves more files.
    if (m_datasetPage) m_datasetPage->stopBackgroundWork();

    // Save before the fade.
    report(m_data.saveSettings());
    report(m_data.saveDefinitions());
    if (m_composerPage) m_composerPage->saveSession(m_data.dataPath(paths::kSession));

    // Fade child windows (the preview popout) too.
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (widget == this || !widget->isWindow() || !widget->isVisible()) continue;
        propertyAnimate(widget, "windowOpacity", widget->windowOpacity(), 0.0, 500,
                        QEasingCurve::InOutSine);
    }

    QPropertyAnimation* out =
        propertyAnimate(this, "windowOpacity", windowOpacity(), 0.0, 500,
                        QEasingCurve::InOutSine);
    connect(out, &QPropertyAnimation::finished, this, [this]() {
        hide();
        // Keeps a delete-on-close popout from outliving quit().
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget == this || !widget->isWindow()) continue;
            widget->setAttribute(Qt::WA_DeleteOnClose, false);
            widget->hide();
            widget->deleteLater();
        }
        QApplication::quit();
    });
}

void AppWindow::report(const QString& error)
{
    if (!error.isEmpty()) qWarning("%s", qUtf8Printable(error));
}

void AppWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (m_loaded) return;
    m_loaded = true;

    // Queued so the window paints before the library loads.
    QTimer::singleShot(0, this, [this]() { m_data.load(m_dataDir); });
}

QWidget* AppWindow::buildPage(int index)
{
    if (kPages[index].page == Page::Home) return new HomePage;

    if (kPages[index].page == Page::EntryViewer) {
        m_viewerPage = new EntryViewerPage(m_data.entries, m_data.search, m_data.composer,
                                           m_data.settings, *m_comfy);
        connect(m_viewerPage, &EntryViewerPage::statusMessage, m_status,
                &StatusBar::showMessage);
        return m_viewerPage;
    }

    if (kPages[index].page == Page::TagComposer) {
        m_composerPage = new ComposerPage(m_data.composer, m_data, m_data.entries,
                                          *m_inputCache, *m_comfy, *m_history);
        connect(m_composerPage, &ComposerPage::statusMessage, m_status, &StatusBar::showMessage);
        connect(m_composerPage, &ComposerPage::workflowEditorRequested, this,
                [this]() { m_pages->setCurrentIndex(int(Page::WorkflowEditor)); });
        connect(m_composerPage, &ComposerPage::facetEditorRequested, this,
                [this](const QString& tag) {
                    m_pages->setCurrentIndex(int(Page::FacetEditor));
                    if (m_facetPage) m_facetPage->selectTagByName(tag);
                });
        connect(m_composerPage, &ComposerPage::wikiRequested, this,
                [this](const QString& tag) {
                    m_pages->setCurrentIndex(int(Page::DanbooruWiki));
                    if (m_wikiPage) m_wikiPage->lookupTag(tag);
                });

        connect(m_composerPage, &ComposerPage::quickFacetRequested, this,
                &AppWindow::addQuickFacet);
        return m_composerPage;
    }

    if (kPages[index].page == Page::OutputViewer) {
        m_outputPage = new OutputViewerPage;
        return m_outputPage;
    }

    if (kPages[index].page == Page::PromptHistory) {
        m_historyPage =
            new PromptHistoryPage(*m_history, m_data.entries, *m_composerPage, *m_comfy);
        connect(m_historyPage, &PromptHistoryPage::statusMessage, m_status,
                &StatusBar::showMessage);
        connect(m_historyPage, &PromptHistoryPage::switchToComposerRequested, this,
                [this]() { m_pages->setCurrentIndex(int(Page::TagComposer)); });
        connect(m_historyPage, &PromptHistoryPage::openEntryRequested, this,
                [this](const QString& uuid) {
                    m_pages->setCurrentIndex(int(Page::EntryViewer));
                    if (m_viewerPage) m_viewerPage->showEntry(uuid);
                });
        return m_historyPage;
    }

    if (kPages[index].page == Page::DanbooruWiki) {
        m_wikiPage = new TagWikiPage;
        // Wiki links go through lookupTag so history is kept in one place.
        connect(m_wikiPage, &TagWikiPage::wikiLinkClicked, m_wikiPage,
                &TagWikiPage::lookupTag);
        return m_wikiPage;
    }

    if (kPages[index].page == Page::FacetEditor) {
        m_facetPage = new FacetEditorPage(m_data);
        connect(m_facetPage, &FacetEditorPage::statusMessage, m_status, &StatusBar::showMessage);
        connect(m_facetPage, &FacetEditorPage::composerRequested, this,
                [this]() { m_pages->setCurrentIndex(int(Page::TagComposer)); });
        connect(m_facetPage, &FacetEditorPage::wikiRequested, this,
                [this](const QString& tag) {
                    m_pages->setCurrentIndex(int(Page::DanbooruWiki));
                    if (m_wikiPage) m_wikiPage->lookupTag(tag);
                });

        // For the undefined-tags list.
        m_facetPage->setActiveTagsProvider(
            [this]() { return m_data.composer.doc().activeTags; });

        connect(m_facetPage, &FacetEditorPage::facetsDefined, this, [this]() {
            if (m_composerPage) m_composerPage->refresh();
        });
        return m_facetPage;
    }

    if (kPages[index].page == Page::WorkflowEditor) {
        m_workflowPage = new WorkflowEditPage(m_data, m_data.entries, m_data.search,
                                              *m_inputCache);
        connect(m_workflowPage, &WorkflowEditPage::statusMessage, m_status,
                &StatusBar::showMessage);
        return m_workflowPage;
    }

    if (kPages[index].page == Page::Settings) {
        m_settingsPage = new SettingsPage(m_data.settings, m_data);
        connect(m_settingsPage, &SettingsPage::reconnectRequested, this,
                [this]() { m_comfy->connectToServer(); });
        connect(m_settingsPage, &SettingsPage::settingsChanged, this, [this]() {
            m_comfy->setServerAddress(m_data.settings.comfyServerAddress);
            m_comfy->setApiKey(m_data.settings.comfyApiKey);
            if (m_data.settings.comfyEnabled)
                m_comfy->connectToServer();
            else
                m_comfy->disconnectFromServer();
            if (m_outputPage)
                m_outputPage->setOutputFolder(m_data.settings.comfyOutputFolder);
            if (m_composerPage) m_composerPage->applySettings();

            const QString error = m_data.saveSettings();
            if (!error.isEmpty()) m_status->showMessage(error);
        });
        return m_settingsPage;
    }
    if (kPages[index].page == Page::DatasetHelpers) {
        m_taggers = std::make_unique<TaggerLibrary>(m_data.dataPath(paths::kModelsDir));
        m_datasetPage = new DatasetHelpersPage(m_data.settings, m_data.defsFile.defs, *m_taggers,
                                               m_data.dataPath(paths::kClusterFilters),
                                               m_data.dataPath(paths::kCollectionsDir));
        connect(m_datasetPage, &DatasetHelpersPage::tabChanged, this,
                [this](const QString&) { updateTitle(); });

        TagClusterPage* cluster = m_datasetPage->clusterPage();
        connect(cluster, &TagClusterPage::wikiRequested, this, [this](const QString& tag) {
            m_pages->setCurrentIndex(int(Page::DanbooruWiki));
            if (m_wikiPage) m_wikiPage->lookupTag(tag);
        });
        connect(cluster, &TagClusterPage::facetEditorRequested, this, [this](const QString& tag) {
            m_pages->setCurrentIndex(int(Page::FacetEditor));
            if (m_facetPage) m_facetPage->selectTagByName(tag);
        });
        connect(cluster, &TagClusterPage::quickFacetRequested, this,
                [this](const QString& tag, const QString& facet) { addQuickFacet(tag, facet); });
        connect(cluster, &TagClusterPage::createEntryRequested, this,
                [this](const QString& title, const QStringList& tags) {
                    // A placeholder image slot; the user drops a real file on it later.
                    Entry entry;
                    entry.title = title.trimmed();
                    entry.created = QDateTime::currentSecsSinceEpoch();
                    entry.images.append(EntryImage{u"00001.png"_s, tags});

                    const QString uuid = m_data.entries.add(std::move(entry));
                    if (uuid.isEmpty()) {
                        m_status->showMessage(u"Could not create the entry."_s);
                        return;
                    }
                    m_status->showMessage(u"Created %1"_s.arg(title));
                    m_pages->setCurrentIndex(int(Page::EntryViewer));
                    if (m_viewerPage) m_viewerPage->showEntry(uuid);
                });
        return m_datasetPage;
    }

    return placeholder(QString::fromLatin1(kPages[index].title));
}

// Writes the definition immediately.
void AppWindow::addQuickFacet(const QString& tag, const QString& facet)
{
    QStringList facets = m_data.defsFile.defs.facetsFor(tag);
    if (facets.contains(facet)) {
        m_status->showMessage(u"%1 already has %2"_s.arg(tag, facet));
        return;
    }
    facets << facet;
    m_data.defsFile.defs.set(tag, facets);

    const QString error = m_data.saveDefinitions();
    m_status->showMessage(error.isEmpty() ? u"%1: added %2"_s.arg(tag, facet) : error);

    if (m_composerPage) m_composerPage->refresh();
    if (m_facetPage) m_facetPage->reload();
    if (m_datasetPage) m_datasetPage->refreshFacets();
}

void AppWindow::installShortcuts()
{
    // Shortcuts don't fire while typing in a text field.
    auto typing = []() {
        const QWidget* focused = qApp->focusWidget();
        return focused
            && (qobject_cast<const QLineEdit*>(focused)
                || qobject_cast<const QPlainTextEdit*>(focused));
    };

    // WindowShortcut so the preview popout can bind the same keys.
    auto add = [this](const QKeySequence& keys, auto handler) {
        auto* shortcut = new QShortcut(keys, this);
        shortcut->setContext(Qt::WindowShortcut);
        connect(shortcut, &QShortcut::activated, this, handler);
    };

    add(QKeySequence(u"Shift+E"_s), [this, typing]() {
        if (typing() || !m_composerPage) return;
        m_composerPage->triggerRun();
    });
    add(QKeySequence(u"Shift+R"_s), [this, typing]() {
        if (typing()) return;
        m_comfy->interrupt();
    });
    add(QKeySequence(u"Shift+Alt+R"_s), [this, typing]() {
        if (typing()) return;
        m_comfy->clearPending();
    });

    add(QKeySequence(u"F11"_s), [this]() {
        QPropertyAnimation* out = propertyAnimate(this, "windowOpacity", windowOpacity(), 0.0,
                                                  200, QEasingCurve::InOutSine);
        connect(out, &QPropertyAnimation::finished, this, [this]() {
            if (isFullScreen())
                showNormal();
            else
                showFullScreen();
            propertyAnimate(this, "windowOpacity", windowOpacity(), 1.0, 200,
                            QEasingCurve::InOutSine);
        });
    });

    add(QKeySequence(u"Ctrl+W"_s), [this]() { close(); });

    add(QKeySequence(u"Ctrl+H"_s), [this]() {
        if (windowState() & Qt::WindowMinimized) return;
        QPropertyAnimation* out = propertyAnimate(this, "windowOpacity", windowOpacity(), 0.0,
                                                  200, QEasingCurve::InOutSine);
        connect(out, &QPropertyAnimation::finished, this, [this]() { showMinimized(); });
    });

    add(QKeySequence(Qt::CTRL | Qt::Key_PageUp), [this]() {
        const int current = m_pages->currentIndex();
        m_pages->setCurrentIndex(current > 0 ? current - 1 : m_pages->count() - 1);
    });
    add(QKeySequence(Qt::CTRL | Qt::Key_PageDown), [this]() {
        const int current = m_pages->currentIndex();
        m_pages->setCurrentIndex(current < m_pages->count() - 1 ? current + 1 : 0);
    });
}

void AppWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && isFullScreen()) {
        QPropertyAnimation* out = propertyAnimate(this, "windowOpacity", windowOpacity(), 0.0,
                                                  200, QEasingCurve::InOutSine);
        connect(out, &QPropertyAnimation::finished, this, [this]() {
            showNormal();
            propertyAnimate(this, "windowOpacity", windowOpacity(), 1.0, 200,
                            QEasingCurve::InOutSine);
        });
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void AppWindow::updateTitle()
{
    const int index = m_pages->currentIndex();
    if (index < 0 || index >= kPageCount) return;

    if (kPages[index].page == Page::TagComposer) {
        setWindowTitle(u"Tag Composer"_s);
        return;
    }
    setWindowTitle(u"Tag Composer - "_s + QString::fromLatin1(kPages[index].title));
}

void AppWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() != QEvent::WindowStateChange) return;

    auto* stateChange = static_cast<QWindowStateChangeEvent*>(event);
    const bool wasMinimized = stateChange->oldState() & Qt::WindowMinimized;
    const bool isMinimized = windowState() & Qt::WindowMinimized;
    if (wasMinimized && !isMinimized && windowOpacity() < 0.99)
        propertyAnimate(this, "windowOpacity", windowOpacity(), 1.0, 200,
                        QEasingCurve::InOutSine);

    m_chrome->onWindowStateChanged();
}

} // namespace tc
