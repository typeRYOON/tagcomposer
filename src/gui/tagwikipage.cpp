#include <gui/tagwikipage.h>
#include <utils/dtext.h>
#include <gui/widgets/composericons.h>
#include <utils/stringutils.h>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDesktopServices>
#include <QRegularExpression>
#include <QFrame>
#include <QHBoxLayout>
#include <QFontDatabase>
#include <QFont>
#include <QShortcut>
#include <QPixmap>
#include <QPainter>
#include <QTimer>
#include <QEvent>
#include <QShowEvent>
#include <QWindow>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QWheelEvent>


namespace gui {

static constexpr int ThumbW = 150;
static constexpr int ThumbH = 150;


// ---- ctor

TagWikiPage::TagWikiPage(QWidget* parent) : QWidget(parent), m_nam(new QNetworkAccessManager(this))
{
    setObjectName("TagWikiPage");
    setAttribute(Qt::WA_StyledBackground, true);

    // ---- Header
    m_titleLabel = new QLabel(this);
    m_titleLabel->setAttribute(Qt::WA_StyledBackground, true);
    m_titleLabel->setObjectName("WikiTitle");
    m_titleLabel->setWordWrap(true);

    m_aliasLabel = new QLabel(this);
    m_aliasLabel->setAttribute(Qt::WA_StyledBackground, true);
    m_aliasLabel->setObjectName("WikiAlias");
    m_aliasLabel->setWordWrap(true);
    m_aliasLabel->hide();

    auto* headerInner = new QWidget;
    headerInner->setObjectName("WikiHeaderInner");
    headerInner->setAttribute(Qt::WA_StyledBackground, true);
    auto* headerInnerLayout = new QVBoxLayout(headerInner);
    headerInnerLayout->setContentsMargins(8, 10, 8, 10);
    headerInnerLayout->setSpacing(2);
    headerInnerLayout->addWidget(m_titleLabel);
    headerInnerLayout->addWidget(m_aliasLabel);
    headerInner->setMaximumWidth(800);
    headerInner->setMinimumWidth(200);
    headerInner->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* header = new QWidget;
    header->setObjectName("WikiHeader");
    header->setAttribute(Qt::WA_StyledBackground, true);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(0);
    headerLayout->addStretch(1);
    headerLayout->addWidget(headerInner, 0);
    headerLayout->addStretch(1);

    // ---- Text browser
    m_browser = new QTextBrowser;
    m_browser->setObjectName("WikiBrowser");
    m_browser->setOpenLinks(false);
    m_browser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_browser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_browser->setFocusPolicy(Qt::NoFocus);
    m_browser->setFrameShape(QFrame::NoFrame);
    m_browser->document()->setDocumentMargin(0);
    connect(m_browser, &QTextBrowser::anchorClicked, this, &TagWikiPage::onAnchorClicked);
    m_browser->installEventFilter(this);
    // Wheel events go to the viewport, not the scroll area itself.
    m_browser->viewport()->installEventFilter(this);

    m_scrollAnim = new QPropertyAnimation(m_browser->verticalScrollBar(), "value", this);
    m_scrollAnim->setDuration(220);
    m_scrollAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_thumbFadeTimer = new QTimer(this);
    m_thumbFadeTimer->setInterval(25);
    connect(m_thumbFadeTimer, &QTimer::timeout, this, &TagWikiPage::onThumbFadeTick);

    // 250 ms between metadata fetches (4 req/sec). Danbooru throttles bursts;
    // pacing keeps us comfortably under the per-IP limit.
    m_thumbFetchTimer = new QTimer(this);
    m_thumbFetchTimer->setInterval(250);
    connect(m_thumbFetchTimer, &QTimer::timeout, this, &TagWikiPage::processThumbFetchQueue);

    // ---- Content widget
    auto* contentWidget = new QWidget;
    auto* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(header);
    contentLayout->addWidget(m_browser, 1);

    // ---- Loading / not-found placeholders
    // Loading state is intentionally blank: the search bar is hint enough.
    auto* loadingLabel = new QLabel();
    loadingLabel->setObjectName("WikiStatusLabel");
    loadingLabel->setAlignment(Qt::AlignCenter);

    auto* notFoundLabel = new QLabel;
    notFoundLabel->setObjectName("WikiStatusLabel");
    notFoundLabel->setAlignment(Qt::AlignCenter);

    m_mainStack = new QStackedWidget;
    m_mainStack->addWidget(loadingLabel);  // 0
    m_mainStack->addWidget(contentWidget); // 1
    m_mainStack->addWidget(notFoundLabel); // 2

    // Effect lives on the stack so all three children share one opacity.
    m_fadeEffect = new QGraphicsOpacityEffect(m_mainStack);
    m_fadeEffect->setOpacity(1.0);
    m_mainStack->setGraphicsEffect(m_fadeEffect);
    m_fadeAnim = new QPropertyAnimation(m_fadeEffect, "opacity", this);
    m_fadeAnim->setDuration(180);
    m_fadeAnim->setEasingCurve(QEasingCurve::InOutSine);
    connect(m_fadeAnim, &QPropertyAnimation::finished, this, [this]() {
        // Only the fade-out direction triggers the next step; fade-in just lands.
        if (m_fadeAnim->endValue().toReal() >= 0.5) return;

        // History nav wins over a queued wiki-link tag if both are set.
        if (m_pendingHistoryNav) {
            m_pendingHistoryNav = false;
            m_pendingTag.clear();
            m_pendingFadeIn = true;
            loadFromHistory();
            return;
        }
        if (!m_pendingTag.isEmpty()) {
            const QString tag = m_pendingTag;
            m_pendingTag.clear();
            m_pendingFadeIn = true;
            emit wikiLinkClicked(tag);
        }
    });

    m_searchBar = new TagSearchBar(this);
    connect(m_searchBar, &TagSearchBar::tagAdded, this, &TagWikiPage::startFadeOutThenLookup);

    m_backBtn = new QPushButton("←");
    m_backBtn->setObjectName("WikiNavBtn");
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setToolTip("Back (Alt+Left)");
    connect(m_backBtn, &QPushButton::clicked, this, &TagWikiPage::goBack);

    m_forwardBtn = new QPushButton("→");
    m_forwardBtn->setObjectName("WikiNavBtn");
    m_forwardBtn->setCursor(Qt::PointingHandCursor);
    m_forwardBtn->setToolTip("Forward (Alt+Right)");
    connect(m_forwardBtn, &QPushButton::clicked, this, &TagWikiPage::goForward);

    m_openExternalBtn = new QPushButton;
    m_openExternalBtn->setObjectName("WikiNavBtn");
    m_openExternalBtn->setCursor(Qt::PointingHandCursor);
    m_openExternalBtn->setToolTip("Open this page on danbooru.donmai.us");
    m_openExternalBtn->setIcon(icons::openExternal());
    m_openExternalBtn->setIconSize(QSize(14, 14));
    connect(m_openExternalBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentTag.isEmpty()) return;
        const QByteArray encoded = QUrl::toPercentEncoding(m_currentTag);
        QDesktopServices::openUrl(QUrl(
            QString("https://danbooru.donmai.us/wiki_pages/%1").arg(QString::fromLatin1(encoded))));
    });

    auto* topBar = new QWidget;
    auto* topBarLayout = new QHBoxLayout(topBar);
    topBarLayout->setContentsMargins(4, 0, 4, 0);
    topBarLayout->setSpacing(2);
    topBarLayout->addWidget(m_backBtn);
    topBarLayout->addWidget(m_forwardBtn);
    topBarLayout->addWidget(m_searchBar, 1);
    topBarLayout->addWidget(m_openExternalBtn);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(topBar);
    root->addWidget(m_mainStack, 1);

    auto* backShortcut = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Left), this);
    backShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(backShortcut, &QShortcut::activated, this, &TagWikiPage::goBack);

    auto* fwdShortcut = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Right), this);
    fwdShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(fwdShortcut, &QShortcut::activated, this, &TagWikiPage::goForward);

    updateNavButtons();
}

// ---- Public API

void TagWikiPage::setDanbooruIndex(core::DanbooruIndex* index)
{
    m_searchBar->setIndex(index);
}

void TagWikiPage::lookupTag(const QString& tag)
{
    if (tag == m_currentTag && m_mainStack->currentIndex() == 1) {
        // Same-tag no-op still has to clear any in-flight fade so we don't
        // leave the page blank.
        finishPendingFadeIn();
        return;
    }

    if (!m_navigating) {
        while (m_history.size() > m_historyPos + 1)
            m_history.removeLast();
        if (m_history.isEmpty() || m_history.last() != tag) {
            m_history << tag;
            m_historyPos = m_history.size() - 1;
        }
    }

    m_currentTag = tag;
    updateNavButtons();

    if (m_wikiCache.contains(tag)) {
        QJsonDocument doc = QJsonDocument::fromJson(m_wikiCache[tag]);
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            QStringList others;
            for (const auto& v : obj["other_names"].toArray())
                others << v.toString();
            displayContent(obj["title"].toString(), others, obj["body"].toString());
            return;
        }
    }

    showLoading();
    fetchWikiPage(tag);
}

// ---- History navigation

void TagWikiPage::goBack()
{
    if (m_historyPos <= 0) return;
    --m_historyPos;
    startFadeOutThenHistory();
}

void TagWikiPage::goForward()
{
    if (m_historyPos >= m_history.size() - 1) return;
    ++m_historyPos;
    startFadeOutThenHistory();
}

void TagWikiPage::loadFromHistory()
{
    m_navigating = true;
    lookupTag(m_history[m_historyPos]);
    m_navigating = false;
}

void TagWikiPage::cancelPendingFade()
{
    m_fadeAnim->stop();
    m_pendingTag.clear();
    m_pendingHistoryNav = false;
    m_pendingFadeIn = false;
    m_fadeEffect->setOpacity(1.0);
}

// ---- Smooth scroll

void TagWikiPage::smoothScrollTo(int target)
{
    QScrollBar* sb = m_browser->verticalScrollBar();
    const int clamped = qBound(sb->minimum(), target, sb->maximum());
    if (m_scrollAnim->state() == QAbstractAnimation::Running) m_scrollAnim->stop();
    m_scrollAnim->setStartValue(sb->value());
    m_scrollAnim->setEndValue(clamped);
    m_scrollAnim->start();
}

void TagWikiPage::smoothScrollToAnchor(const QString& anchor)
{
    // QTextBrowser doesn't expose anchor positions; jump, read, snap back,
    // animate. The snap happens before any paint so it isn't visible.
    QScrollBar* sb = m_browser->verticalScrollBar();
    const int from = sb->value();
    m_browser->scrollToAnchor(anchor);
    const int to = sb->value();
    if (to == from) return;
    sb->setValue(from);
    smoothScrollTo(to);
}

// ---- Network

void TagWikiPage::fetchWikiPage(const QString& tag)
{
    const QByteArray encoded = QUrl::toPercentEncoding(tag);
    QUrl url(
        QString("https://danbooru.donmai.us/wiki_pages/%1.json").arg(QString::fromLatin1(encoded)));

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "TagComposer/1.0");
    req.setRawHeader("Accept", "application/json");

    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, tag, reply]() {
        reply->deleteLater();

        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 404) {
            if (tag == m_currentTag) showNotFound(tag);
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            if (tag == m_currentTag) {
                qobject_cast<QLabel*>(m_mainStack->widget(2))
                    ->setText("Network error: " + reply->errorString());
                m_mainStack->setCurrentIndex(2);
                finishPendingFadeIn();
            }
            return;
        }

        const QByteArray data = reply->readAll();
        m_wikiCache[tag] = data;

        if (tag != m_currentTag) return;

        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) {
            showNotFound(tag);
            return;
        }

        QJsonObject obj = doc.object();
        QStringList others;
        for (const auto& v : obj["other_names"].toArray())
            others << v.toString();
        displayContent(obj["title"].toString(), others, obj["body"].toString());
    });
}

void TagWikiPage::downloadThumbAndFade(const QString& imageUrl, const QString& resourceUrl,
                                       std::function<void(const QPixmap&)> store)
{
    if (imageUrl.isEmpty()) return;
    QNetworkRequest imgReq{QUrl(imageUrl)};
    imgReq.setHeader(QNetworkRequest::UserAgentHeader, "TagComposer/1.0");
    auto* imgReply = m_nam->get(imgReq);
    connect(imgReply, &QNetworkReply::finished, this,
            [this, imgReply, resourceUrl, store = std::move(store)]() {
                imgReply->deleteLater();
                if (imgReply->error() != QNetworkReply::NoError) return;

                QPixmap pix;
                if (!pix.loadFromData(imgReply->readAll()) || pix.isNull()) return;

                // Pad onto a fixed ThumbW x ThumbH canvas so the <img> sized
                // to those same dimensions doesn't stretch non-square thumbs.
                const QPixmap scaled =
                    pix.scaled(ThumbW, ThumbH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                QPixmap fitted(ThumbW, ThumbH);
                fitted.fill(Qt::transparent);
                {
                    QPainter cp(&fitted);
                    cp.setRenderHint(QPainter::SmoothPixmapTransform, true);
                    const int x = (ThumbW - scaled.width()) / 2;
                    const int y = (ThumbH - scaled.height()) / 2;
                    cp.drawPixmap(x, y, scaled);
                }

                store(fitted);
                startThumbFade(resourceUrl, fitted);
            });
}

void TagWikiPage::fetchPostData(int postId)
{
    const QString resourceUrl = QString("post:%1").arg(postId);

    if (m_postThumbs.contains(postId)) {
        m_browser->document()->addResource(QTextDocument::ImageResource, QUrl(resourceUrl),
                                           QVariant(m_postThumbs[postId]));
        m_browser->document()->markContentsDirty(0, m_browser->document()->characterCount());
        m_browser->viewport()->update();
        return;
    }

    QNetworkRequest req(QUrl(QString("https://danbooru.donmai.us/posts/%1.json").arg(postId)));
    req.setHeader(QNetworkRequest::UserAgentHeader, "TagComposer/1.0");
    req.setRawHeader("Accept", "application/json");

    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, postId, resourceUrl, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) return;

        const QString previewUrl = doc.object()["preview_file_url"].toString();
        downloadThumbAndFade(previewUrl, resourceUrl, [this, postId](const QPixmap& fitted) {
            m_postThumbs[postId] = fitted;
        });
    });
}

void TagWikiPage::fetchAssetData(int assetId)
{
    const QString resourceUrl = QString("asset:%1").arg(assetId);

    if (m_assetThumbs.contains(assetId)) {
        m_browser->document()->addResource(QTextDocument::ImageResource, QUrl(resourceUrl),
                                           QVariant(m_assetThumbs[assetId]));
        m_browser->document()->markContentsDirty(0, m_browser->document()->characterCount());
        m_browser->viewport()->update();
        return;
    }

    QNetworkRequest req(
        QUrl(QString("https://danbooru.donmai.us/media_assets/%1.json").arg(assetId)));
    req.setHeader(QNetworkRequest::UserAgentHeader, "TagComposer/1.0");
    req.setRawHeader("Accept", "application/json");

    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, assetId, resourceUrl, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) return;

        // Pick the smallest available variant; first-of-array as last resort.
        const QJsonArray variants = doc.object()["variants"].toArray();
        static const QStringList preferred = {"180x180", "360x360", "720x720", "sample",
                                              "original"};
        QString thumbUrl;
        for (const QString& wanted : preferred) {
            for (const QJsonValue& v : variants) {
                if (v.toObject()["type"].toString() == wanted) {
                    thumbUrl = v.toObject()["url"].toString();
                    break;
                }
            }
            if (!thumbUrl.isEmpty()) break;
        }
        if (thumbUrl.isEmpty() && !variants.isEmpty())
            thumbUrl = variants.first().toObject()["url"].toString();

        downloadThumbAndFade(thumbUrl, resourceUrl, [this, assetId](const QPixmap& fitted) {
            m_assetThumbs[assetId] = fitted;
        });
    });
}

// ---- Display

void TagWikiPage::showLoading()
{
    m_mainStack->setCurrentIndex(0);
}

void TagWikiPage::showNotFound(const QString& tag)
{
    qobject_cast<QLabel*>(m_mainStack->widget(2))
        ->setText(QString("No wiki page found for \"%1\".").arg(tag));
    m_mainStack->setCurrentIndex(2);
    finishPendingFadeIn();
}

void TagWikiPage::displayContent(const QString& title, const QStringList& otherNames,
                                 const QString& body)
{
    // Drop any thumb fades and pending metadata fetches from a prior page;
    // their resource urls don't exist in the document we're about to install.
    m_pendingFades.clear();
    if (m_thumbFadeTimer->isActive()) m_thumbFadeTimer->stop();
    m_thumbFetchQueue.clear();
    if (m_thumbFetchTimer->isActive()) m_thumbFetchTimer->stop();

    m_titleLabel->setText(title.isEmpty() ? m_currentTag : utils::normalizeTagInput(title));

    if (!otherNames.isEmpty()) {
        m_aliasLabel->setText("Also known as: " + otherNames.join(", "));
        m_aliasLabel->show();
    }
    else {
        m_aliasLabel->hide();
    }

    QList<int> postIds;
    QList<int> assetIds;
    const QString html = dtextToHtml(body, postIds, assetIds);

    // Transparent placeholder for not-yet-fetched thumbs: stops the broken-
    // image glyph from flashing and pins layout for the eventual fade-in.
    QPixmap placeholder(ThumbW, ThumbH);
    placeholder.fill(Qt::transparent);
    for (int id : postIds) {
        m_browser->document()->addResource(QTextDocument::ImageResource,
                                           QUrl(QString("post:%1").arg(id)),
                                           QVariant(m_postThumbs.value(id, placeholder)));
    }
    for (int id : assetIds) {
        m_browser->document()->addResource(QTextDocument::ImageResource,
                                           QUrl(QString("asset:%1").arg(id)),
                                           QVariant(m_assetThumbs.value(id, placeholder)));
    }

    if (!m_fontApplied) {
        const QStringList families = QFontDatabase::applicationFontFamilies(0);
        if (!families.isEmpty()) {
            m_browser->setFont(QFont(families.first(), 15));
            m_fontApplied = true;
        }
    }

    // setHtml resets scrollbar range; in-flight scroll target would be stale.
    if (m_scrollAnim->state() == QAbstractAnimation::Running) m_scrollAnim->stop();

    m_browser->setHtml(html);
    m_mainStack->setCurrentIndex(1);
    finishPendingFadeIn();

    for (int id : postIds)
        if (!m_postThumbs.contains(id)) enqueueThumbFetch(ThumbKind::Post, id);
    for (int id : assetIds)
        if (!m_assetThumbs.contains(id)) enqueueThumbFetch(ThumbKind::Asset, id);
}

// ---- Resize / screen-change re-layout

bool TagWikiPage::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_browser && event->type() == QEvent::Resize) {
        // QTextDocument's lazy re-flow can leave image cells overlapping
        // text after a width change. Force a full re-layout.
        if (auto* doc = m_browser->document()) doc->markContentsDirty(0, doc->characterCount());
    }
    if (obj == m_browser->viewport() && event->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(event);
        // Skip smoothing for zoom modifiers and precision-touchpad pixel deltas.
        if (we->modifiers() != Qt::NoModifier) return false;
        if (!we->pixelDelta().isNull()) return false;

        const int notches = we->angleDelta().y() / 120;
        if (notches == 0) return false;
        const int delta = -notches * 60;
        // Use in-flight target as base so rapid notches stack instead of restart.
        QScrollBar* sb = m_browser->verticalScrollBar();
        const int base = (m_scrollAnim->state() == QAbstractAnimation::Running)
                             ? m_scrollAnim->endValue().toInt()
                             : sb->value();
        smoothScrollTo(base + delta);
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void TagWikiPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    // Same-size moves to a different-DPR monitor don't fire Resize but do
    // leave stale image-cell metrics. screenChanged catches that case.
    if (!m_screenChangedConnected) {
        if (QWidget* topLevel = window()) {
            if (QWindow* handle = topLevel->windowHandle()) {
                connect(handle, &QWindow::screenChanged, this, [this](QScreen*) {
                    if (auto* doc = m_browser->document())
                        doc->markContentsDirty(0, doc->characterCount());
                });
                m_screenChangedConnected = true;
            }
        }
    }
}

// ---- Thumbnail fade
void TagWikiPage::startThumbFade(const QString& resourceUrl, const QPixmap& finalPix)
{
    // Enqueue and let the shared ticker handle it. A late re-fetch of an
    // already-fading url just resets the alpha ramp.
    for (auto& f : m_pendingFades) {
        if (f.resourceUrl == resourceUrl) {
            f.finalPix = finalPix;
            f.step = 0;
            if (!m_thumbFadeTimer->isActive()) m_thumbFadeTimer->start();
            return;
        }
    }
    m_pendingFades.append({resourceUrl, finalPix, 0});
    if (!m_thumbFadeTimer->isActive()) m_thumbFadeTimer->start();
}

void TagWikiPage::onThumbFadeTick()
{
    constexpr int totalMs = 220;
    constexpr int stepMs = 25;
    constexpr int totalSteps = totalMs / stepMs;

    bool any = false;
    for (int i = m_pendingFades.size() - 1; i >= 0; --i) {
        PendingThumbFade& f = m_pendingFades[i];
        ++f.step;
        const float alpha = qMin(1.0f, float(f.step) / float(totalSteps));

        QPixmap faded(f.finalPix.size());
        faded.fill(Qt::transparent);
        QPainter p(&faded);
        p.setOpacity(alpha);
        p.drawPixmap(0, 0, f.finalPix);
        p.end();

        m_browser->document()->addResource(QTextDocument::ImageResource, QUrl(f.resourceUrl),
                                           QVariant(faded));
        any = true;

        if (f.step >= totalSteps) m_pendingFades.removeAt(i);
    }

    if (any) {
        m_browser->document()->markContentsDirty(0, m_browser->document()->characterCount());
        m_browser->viewport()->update();
    }
    if (m_pendingFades.isEmpty()) m_thumbFadeTimer->stop();
}

// ---- Paced thumb metadata fetch

void TagWikiPage::enqueueThumbFetch(ThumbKind kind, int id)
{
    m_thumbFetchQueue.append({kind, id});
    if (!m_thumbFetchTimer->isActive()) m_thumbFetchTimer->start();
}

void TagWikiPage::processThumbFetchQueue()
{
    if (m_thumbFetchQueue.isEmpty()) {
        m_thumbFetchTimer->stop();
        return;
    }
    const QueuedThumbFetch job = m_thumbFetchQueue.takeFirst();
    if (job.kind == ThumbKind::Post) fetchPostData(job.id);
    else fetchAssetData(job.id);
}

// ---- Nav button state

void TagWikiPage::updateNavButtons()
{
    if (m_backBtn) m_backBtn->setEnabled(m_historyPos > 0);
    if (m_forwardBtn) m_forwardBtn->setEnabled(m_historyPos < m_history.size() - 1);
    if (m_openExternalBtn) m_openExternalBtn->setEnabled(!m_currentTag.isEmpty());
}

// ---- Page transition helpers

void TagWikiPage::startFadeOutThenLookup(const QString& tag)
{
    m_pendingTag = tag;
    m_pendingHistoryNav = false; // wiki-link click overrides any pending history nav
    m_fadeAnim->stop();
    m_fadeAnim->setStartValue(m_fadeEffect->opacity());
    m_fadeAnim->setEndValue(0.0);
    m_fadeAnim->start();
}

void TagWikiPage::startFadeOutThenHistory()
{
    m_pendingHistoryNav = true;
    m_pendingTag.clear(); // history nav overrides any pending wiki-link tag
    m_fadeAnim->stop();
    m_fadeAnim->setStartValue(m_fadeEffect->opacity());
    m_fadeAnim->setEndValue(0.0);
    m_fadeAnim->start();
}

void TagWikiPage::finishPendingFadeIn()
{
    if (!m_pendingFadeIn) return;
    m_pendingFadeIn = false;
    m_fadeAnim->stop();
    m_fadeAnim->setStartValue(m_fadeEffect->opacity());
    m_fadeAnim->setEndValue(1.0);
    m_fadeAnim->start();
}

// ---- Anchor clicks

void TagWikiPage::onAnchorClicked(const QUrl& url)
{
    if (url.scheme() == "wiki") {
        startFadeOutThenLookup(url.path());
        return;
    }
    if (url.scheme() == "post") {
        QDesktopServices::openUrl(
            QUrl(QString("https://danbooru.donmai.us/posts/%1").arg(url.path())));
        return;
    }
    if (url.scheme() == "asset") {
        QDesktopServices::openUrl(
            QUrl(QString("https://danbooru.donmai.us/media_assets/%1").arg(url.path())));
        return;
    }

    // Same-page anchor link (e.g. table-of-contents jumps). The href starts
    // with `#`, but Qt may resolve it against the (empty) document source and
    // leave a non-empty scheme on the URL. Don't gate on scheme - if there's
    // a fragment and no special scheme matched above, treat it as an anchor.
    if (!url.fragment().isEmpty()) {
        smoothScrollToAnchor(url.fragment());
        return;
    }

    QDesktopServices::openUrl(url);
}

// ---- DText -> HTML

QString TagWikiPage::dtextToHtml(const QString& dtext, QList<int>& outPostIds,
                                 QList<int>& outAssetIds)
{
    // Markup conversion lives in utils so the facet editor can render the same
    // wiki text; only the document wrapper below is page-specific.
    const QString text = utils::dtextToHtml(dtext, &outPostIds, &outAssetIds, ThumbW, ThumbH);

    const QStringList fontFamilies = QFontDatabase::applicationFontFamilies(0);
    const QString fontDecl =
        fontFamilies.isEmpty() ? QString()
                               : QString("font-family:'%1',sans-serif;").arg(fontFamilies.first());

    const QString css =
        "<style>"
        "body{background:transparent;color:#c0c0c0;font-size:20px;margin:0;padding:0;" +
        fontDecl +
        "}"
        "h1,h2,h3,h4,h5,h6{color:#888;border-bottom:1px solid "
        "#222;padding-bottom:3px;margin-top:14px;}"
        "a{color:#66aa66;text-decoration:none;}"
        "blockquote{border-left:2px solid #333;margin:4px 0 4px 8px;padding-left:10px;color:#888;}"
        "code{background:#1a1a1a;border-radius:3px;padding:1px "
        "4px;font-family:monospace;font-size:12px;}"
        "ul,ol{padding-left:20px;margin:4px 0;}"
        "li{margin:2px 0;}"
        "table.gallery{margin:6px 0;}"
        "td.thumb{color:#888;font-size:20px;}"
        ".ws{margin:6px 0;}"
        ".wsh{color:#666;font-weight:bold;font-size:11px;margin:0 0 3px 0;}"
        ".wtn{color:#666;font-size:11px;}"
        ".wsp{color:#555;}"
        "</style>";

    return "<html><head>" + css +
           "</head><body>"
           "<table width='100%' cellspacing='0' cellpadding='0'>"
           "<tr><td></td>"
           "<td width='800' style='padding:16px 8px;'>" +
           text +
           "</td>"
           "<td></td></tr>"
           "</table></body></html>";
}

} // namespace gui
