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

// ComfyUI drops the queue count before the save node has written the
// file. Reading straight away finds the previous run's image.
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
    // Flags first: on a window setWindowFlags re-creates the native window
    // and drops the geometry set before it, and what the layout then falls
    // back to is the stack's size hint, i.e. the largest page there is.
    //
    // The hints are spelled out rather than OR'd onto windowFlags() because a
    // frameless window still needs them for the taskbar: without them Windows
    // gives up snap, the minimise animation and the thumbnail.
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinimizeButtonHint
                   | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);

    // The composer is the constraint: a 330px sidebar, the floating control
    // bar, and a tag list wide enough that rows are not all ellipsis.
    resize(1550, 872);
    setMinimumSize(1420, 920);

    m_chrome = new WindowChrome(this);
    setCentralWidget(m_chrome->frame());

    // Before the pages: buildPage wires a page's status messages into it.
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
        // The editor shows whatever the composer has selected, and that can
        // change while this page is off screen.
        if (m_workflowPage && index == int(Page::WorkflowEditor)) m_workflowPage->refresh();
    });
    connect(m_nav, &NavBar::discordRequested, this,
            []() { QDesktopServices::openUrl(QUrl(QString::fromLatin1(kDiscordUrl))); });

    // The live feed: previews to the composer, step and queue to the bar.
    connect(m_comfy, &ComfyClient::previewImageReady, this, [this](const QImage& image) {
        if (m_composerPage) m_composerPage->setPreviewImage(image);
    });
    connect(m_comfy, &ComfyClient::previewProgressChanged, this, [this](int step, int total) {
        m_status->setProgress(step, total);
        if (m_composerPage) m_composerPage->setComfyProgress(step, total);

        // Something is actually sampling, so there will be an image worth
        // loading when the queue comes back down.
        if (step > 0 && total > 0) m_pendingFinalLoad = true;

        // Back at the first step means the next prompt in the queue has
        // started. The finished image of the one before it is still waiting
        // on its timer, and landing now would drop a stale picture on top of
        // a live preview and then be replaced by the next frame.
        if (step <= 1) ++m_sampleGeneration;
    });
    connect(m_comfy, &ComfyClient::queueCountChanged, this, [this](int count) {
        m_status->setActiveCount(count);
        if (m_composerPage) m_composerPage->setComfyActiveCount(count);

        // A queue that just got shorter means a prompt finished. The live
        // preview frames stop at the last sampler step, so without this the
        // composer keeps showing a half-denoised latent instead of the render.
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

        // ComfyUI reports the queue before the file is on disk, so the read
        // has to wait for the write.
        const int generation = m_sampleGeneration;
        QTimer::singleShot(kFinalLoadDelayMs, this, [this, generation]() {
            if (generation != m_sampleGeneration) return; // a newer run owns the preview now
            loadFinalPreview();
        });
    });

    // An interrupt kills the current prompt, a clear drops the queued ones.
    // Either way the newest temp image is the previous run's and loading it
    // would silently show the wrong picture.
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
            // After the library: a push names an entry, and restoring one
            // before the entries exist would drop every push as missing.
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

    // Fully transparent until the first event-loop turn, so the first frame
    // the user sees is a complete window fading in rather than a half-built
    // one snapping into place.
    setWindowOpacity(0.0);
    QTimer::singleShot(0, this, [this]() {
        propertyAnimate(this, "windowOpacity", 0.0, 1.0, 250, QEasingCurve::InOutSine);
    });
}

AppWindow::~AppWindow() = default;

// The finished render, read back off disk. ComfyUI streams preview frames
// while sampling but never sends the decoded result, so the temp folder is
// the only place the real image exists.
void AppWindow::loadFinalPreview()
{
    if (!m_composerPage) return;

    const QString folder = m_data.settings.comfyTempFolder;
    if (folder.isEmpty()) return;

    static const QStringList filters = {u"*.png"_s, u"*.jpg"_s, u"*.jpeg"_s, u"*.webp"_s};
    const QFileInfoList files = QDir(folder).entryInfoList(filters, QDir::Files);
    if (files.isEmpty()) return;

    // Newest by modification time: the run that just finished is the freshest
    // write, and the folder keeps older ones.
    const QFileInfo* newest = &files[0];
    for (const QFileInfo& info : files)
        if (info.lastModified() > newest->lastModified()) newest = &info;

    const QImage image(newest->absoluteFilePath());
    if (!image.isNull()) m_composerPage->setPreviewImage(image);
}


void AppWindow::closeEvent(QCloseEvent* event)
{
    // Two passes: the first saves and fades, the second lets the close
    // through once the animation has finished.
    if (m_closing) {
        event->accept();
        return;
    }
    event->ignore();
    m_closing = true;

    // Stopped first: the collector's timer moves files, and it must not keep
    // doing that through the fade or after the window is gone.
    if (m_datasetPage) m_datasetPage->stopBackgroundWork();

    // Saved before the fade, so a crash during the animation cannot lose it.
    report(m_data.saveSettings());
    report(m_data.saveDefinitions());
    if (m_composerPage) m_composerPage->saveSession(m_data.dataPath(paths::kSession));

    // Child windows - the preview popout - fade with the main one.
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
        // A popout set to delete on close would otherwise keep the event loop
        // alive after quit().
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

    // Queued so the window is painted before 1,345 entry folders are read.
    // Pages redraw from the store signals they already listen to.
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

        // A quick-add writes the definition straight through, since the point
        // is not having to visit the facet editor for it.
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
        // A [[link]] in the body comes back out so history and the fade stay
        // in one place rather than being duplicated inside the page.
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

        // The undefined list reads whatever the composer currently holds.
        m_facetPage->setActiveTagsProvider(
            [this]() { return m_data.composer.doc().activeTags; });

        // A new definition changes what the pipeline resolves.
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
                    // One placeholder image slot carrying the cluster's tags;
                    // a real file gets dropped onto it later.
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

// Writes the definition straight through: the point of a quick-add is not
// having to visit the facet editor for it.
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
    // Nothing fires while a text field has focus, or typing "e" in the search
    // bar would queue a render.
    auto typing = []() {
        const QWidget* focused = qApp->focusWidget();
        return focused
            && (qobject_cast<const QLineEdit*>(focused)
                || qobject_cast<const QPlainTextEdit*>(focused));
    };

    // WindowShortcut, so a child window such as the preview popout can bind
    // the same keys for itself.
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

    // Page cycling wraps at both ends.
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
    // Only reached when no focused child consumed it first.
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

    // The composer shares the app's name, so it gets no suffix.
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
