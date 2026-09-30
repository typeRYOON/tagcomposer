#include <app/tag_wiki_page.h>
#include <app/dtext.h>
#include <app/icons.h>
#include <app/tag_search_bar.h>
#include <core/entry.h>
#include <QDesktopServices>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

constexpr int kThumbWidth = 150;
constexpr int kThumbHeight = 150;
constexpr auto kUserAgent = "TagComposer/1.0";

QNetworkRequest jsonRequest(const QUrl& url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
    request.setRawHeader("Accept", "application/json");
    return request;
}

QPushButton* navButton(const QString& tooltip)
{
    auto* button = new QPushButton;
    button->setObjectName(u"WikiNavBtn"_s);
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(tooltip);
    return button;
}

} // namespace

TagWikiPage::TagWikiPage(QWidget* parent)
    : QWidget(parent), m_network(new QNetworkAccessManager(this))
{
    setObjectName(u"TagWikiPage"_s);
    setAttribute(Qt::WA_StyledBackground, true);

    // ---- Header
    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName(u"WikiTitle"_s);
    m_titleLabel->setAttribute(Qt::WA_StyledBackground, true);
    m_titleLabel->setWordWrap(true);

    m_aliasLabel = new QLabel(this);
    m_aliasLabel->setObjectName(u"WikiAlias"_s);
    m_aliasLabel->setAttribute(Qt::WA_StyledBackground, true);
    m_aliasLabel->setWordWrap(true);
    m_aliasLabel->hide();

    auto* headerInner = new QWidget;
    headerInner->setObjectName(u"WikiHeaderInner"_s);
    headerInner->setAttribute(Qt::WA_StyledBackground, true);
    headerInner->setMaximumWidth(800);
    headerInner->setMinimumWidth(200);
    headerInner->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* headerInnerLayout = new QVBoxLayout(headerInner);
    headerInnerLayout->setContentsMargins(8, 10, 8, 10);
    headerInnerLayout->setSpacing(2);
    headerInnerLayout->addWidget(m_titleLabel);
    headerInnerLayout->addWidget(m_aliasLabel);

    auto* header = new QWidget;
    header->setObjectName(u"WikiHeader"_s);
    header->setAttribute(Qt::WA_StyledBackground, true);

    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(0);
    headerLayout->addStretch(1);
    headerLayout->addWidget(headerInner, 0);
    headerLayout->addStretch(1);

    // ---- Body
    m_browser = new QTextBrowser;
    m_browser->setObjectName(u"WikiBrowser"_s);
    m_browser->setOpenLinks(false);
    m_browser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_browser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_browser->setFocusPolicy(Qt::NoFocus);
    m_browser->setFrameShape(QFrame::NoFrame);
    m_browser->document()->setDocumentMargin(0);
    m_browser->installEventFilter(this);

    // The wheel lands on the viewport, not on the scroll area itself.
    m_browser->viewport()->installEventFilter(this);
    connect(m_browser, &QTextBrowser::anchorClicked, this, &TagWikiPage::onAnchorClicked);

    m_scrollAnim = new QPropertyAnimation(m_browser->verticalScrollBar(), "value", this);
    m_scrollAnim->setDuration(220);
    m_scrollAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_thumbFadeTimer = new QTimer(this);
    m_thumbFadeTimer->setInterval(25);
    connect(m_thumbFadeTimer, &QTimer::timeout, this, &TagWikiPage::onThumbFadeTick);

    // Four requests a second. Danbooru throttles bursts, and an article with
    // a large gallery would otherwise fire a hundred at once.
    m_thumbFetchTimer = new QTimer(this);
    m_thumbFetchTimer->setInterval(250);
    connect(m_thumbFetchTimer, &QTimer::timeout, this, &TagWikiPage::processThumbFetchQueue);

    auto* content = new QWidget;
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(header);
    contentLayout->addWidget(m_browser, 1);

    // Loading is deliberately blank: the search bar is hint enough, and a
    // spinner would flash on every cached lookup.
    auto* loadingLabel = new QLabel;
    loadingLabel->setObjectName(u"WikiStatusLabel"_s);
    loadingLabel->setAlignment(Qt::AlignCenter);

    auto* notFoundLabel = new QLabel;
    notFoundLabel->setObjectName(u"WikiStatusLabel"_s);
    notFoundLabel->setAlignment(Qt::AlignCenter);

    m_mainStack = new QStackedWidget;
    m_mainStack->addWidget(loadingLabel);  // 0
    m_mainStack->addWidget(content);       // 1
    m_mainStack->addWidget(notFoundLabel); // 2

    // One effect on the stack, so all three children share an opacity and a
    // transition between them cannot cross-fade against itself.
    m_fadeEffect = new QGraphicsOpacityEffect(m_mainStack);
    m_fadeEffect->setOpacity(1.0);
    m_mainStack->setGraphicsEffect(m_fadeEffect);

    m_fadeAnim = new QPropertyAnimation(m_fadeEffect, "opacity", this);
    m_fadeAnim->setDuration(180);
    m_fadeAnim->setEasingCurve(QEasingCurve::InOutSine);
    connect(m_fadeAnim, &QPropertyAnimation::finished, this, [this]() {
        // Only the fade-out half triggers the next step; a fade-in just lands.
        if (m_fadeAnim->endValue().toReal() >= 0.5) return;

        // A history step wins over a queued link when both are pending.
        if (m_pendingHistoryNav) {
            m_pendingHistoryNav = false;
            m_pendingTag.clear();
            m_pendingFadeIn = true;
            loadFromHistory();
            return;
        }
        if (m_pendingTag.isEmpty()) return;

        const QString tag = m_pendingTag;
        m_pendingTag.clear();
        m_pendingFadeIn = true;
        emit wikiLinkClicked(tag);
    });

    // ---- Top bar
    m_searchBar = new TagSearchBar(this);
    connect(m_searchBar, &TagSearchBar::tagAdded, this, &TagWikiPage::startFadeOutThenLookup);

    m_backBtn = navButton(u"Back (Alt+Left)"_s);
    icons::applyStates(m_backBtn, icons::arrowLeft, 14, QColor(0x3a, 0x3a, 0x3a),
                       QColor(0x88, 0x88, 0x88), QColor(0x1e, 0x1e, 0x1e));
    connect(m_backBtn, &QPushButton::clicked, this, &TagWikiPage::goBack);

    m_forwardBtn = navButton(u"Forward (Alt+Right)"_s);
    icons::applyStates(m_forwardBtn, icons::arrowRight, 14, QColor(0x3a, 0x3a, 0x3a),
                       QColor(0x88, 0x88, 0x88), QColor(0x1e, 0x1e, 0x1e));
    connect(m_forwardBtn, &QPushButton::clicked, this, &TagWikiPage::goForward);

    m_openExternalBtn = navButton(u"Open this page on danbooru.donmai.us"_s);
    m_openExternalBtn->setIcon(icons::openExternal());
    m_openExternalBtn->setIconSize(QSize(14, 14));
    connect(m_openExternalBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentTag.isEmpty()) return;
        const QByteArray encoded = QUrl::toPercentEncoding(m_currentTag);
        QDesktopServices::openUrl(QUrl(u"https://danbooru.donmai.us/wiki_pages/%1"_s.arg(
            QString::fromLatin1(encoded))));
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

    auto* forwardShortcut = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Right), this);
    forwardShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(forwardShortcut, &QShortcut::activated, this, &TagWikiPage::goForward);

    updateNavButtons();
}

void TagWikiPage::setIndex(const DanbooruIndex* index)
{
    m_searchBar->setIndex(index);
}

void TagWikiPage::lookupTag(const QString& tag)
{
    if (tag == m_currentTag && m_mainStack->currentIndex() == 1) {
        // Even a no-op has to release a fade already in flight, or the page
        // is left sitting at zero opacity.
        finishPendingFadeIn();
        return;
    }

    if (!m_navigating) {
        // A new lookup truncates whatever was ahead in the history.
        while (m_history.size() > m_historyPos + 1)
            m_history.removeLast();
        if (m_history.isEmpty() || m_history.last() != tag) {
            m_history << tag;
            m_historyPos = int(m_history.size()) - 1;
        }
    }

    m_currentTag = tag;
    updateNavButtons();

    const auto cached = m_wikiCache.constFind(tag);
    if (cached != m_wikiCache.cend()) {
        const QJsonDocument doc = QJsonDocument::fromJson(cached.value());
        if (doc.isObject()) {
            const QJsonObject obj = doc.object();
            QStringList others;
            for (const QJsonValue name : obj[u"other_names"_s].toArray())
                others << name.toString();
            displayContent(obj[u"title"_s].toString(), others, obj[u"body"_s].toString());
            return;
        }
    }

    showLoading();
    fetchWikiPage(tag);
}

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

void TagWikiPage::updateNavButtons()
{
    m_backBtn->setEnabled(m_historyPos > 0);
    m_forwardBtn->setEnabled(m_historyPos < m_history.size() - 1);
    m_openExternalBtn->setEnabled(!m_currentTag.isEmpty());
}

// ---- Scrolling

void TagWikiPage::smoothScrollTo(int target)
{
    QScrollBar* bar = m_browser->verticalScrollBar();
    const int clamped = qBound(bar->minimum(), target, bar->maximum());

    if (m_scrollAnim->state() == QAbstractAnimation::Running) m_scrollAnim->stop();
    m_scrollAnim->setStartValue(bar->value());
    m_scrollAnim->setEndValue(clamped);
    m_scrollAnim->start();
}

void TagWikiPage::smoothScrollToAnchor(const QString& anchor)
{
    // QTextBrowser will not report an anchor's position, so this jumps to it,
    // reads the value, snaps back and animates. The snap happens before any
    // paint, so it is never visible.
    QScrollBar* bar = m_browser->verticalScrollBar();
    const int from = bar->value();

    m_browser->scrollToAnchor(anchor);
    const int to = bar->value();
    if (to == from) return;

    bar->setValue(from);
    smoothScrollTo(to);
}

// ---- Network

void TagWikiPage::fetchWikiPage(const QString& tag)
{
    const QByteArray encoded = QUrl::toPercentEncoding(tag);
    const QUrl url(
        u"https://danbooru.donmai.us/wiki_pages/%1.json"_s.arg(QString::fromLatin1(encoded)));

    QNetworkReply* reply = m_network->get(jsonRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, tag, reply]() {
        reply->deleteLater();

        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 404) {
            if (tag == m_currentTag) showNotFound(tag);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            if (tag != m_currentTag) return;
            qobject_cast<QLabel*>(m_mainStack->widget(2))
                ->setText(u"Network error: "_s + reply->errorString());
            m_mainStack->setCurrentIndex(2);
            finishPendingFadeIn();
            return;
        }

        const QByteArray data = reply->readAll();
        m_wikiCache[tag] = data;

        // The user may have navigated on while this was in flight.
        if (tag != m_currentTag) return;

        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) {
            showNotFound(tag);
            return;
        }

        const QJsonObject obj = doc.object();
        QStringList others;
        for (const QJsonValue name : obj[u"other_names"_s].toArray())
            others << name.toString();
        displayContent(obj[u"title"_s].toString(), others, obj[u"body"_s].toString());
    });
}

void TagWikiPage::downloadThumbAndFade(const QString& imageUrl, const QString& resourceUrl,
                                       std::function<void(const QPixmap&)> store)
{
    if (imageUrl.isEmpty()) return;

    QNetworkRequest request{QUrl(imageUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));

    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, resourceUrl, store = std::move(store)]() {
                reply->deleteLater();
                if (reply->error() != QNetworkReply::NoError) return;

                QPixmap image;
                if (!image.loadFromData(reply->readAll()) || image.isNull()) return;

                // Padded onto a fixed canvas, because the <img> is sized to
                // those dimensions and would otherwise stretch a non-square
                // thumbnail.
                const QPixmap scaled = image.scaled(kThumbWidth, kThumbHeight,
                                                    Qt::KeepAspectRatio,
                                                    Qt::SmoothTransformation);
                QPixmap fitted(kThumbWidth, kThumbHeight);
                fitted.fill(Qt::transparent);
                {
                    QPainter painter(&fitted);
                    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                    painter.drawPixmap((kThumbWidth - scaled.width()) / 2,
                                       (kThumbHeight - scaled.height()) / 2, scaled);
                }

                store(fitted);
                startThumbFade(resourceUrl, fitted);
            });
}

void TagWikiPage::fetchPostData(int postId)
{
    const QString resourceUrl = u"post:%1"_s.arg(postId);

    const auto cached = m_postThumbs.constFind(postId);
    if (cached != m_postThumbs.cend()) {
        m_browser->document()->addResource(QTextDocument::ImageResource, QUrl(resourceUrl),
                                           QVariant(cached.value()));
        m_browser->document()->markContentsDirty(0, m_browser->document()->characterCount());
        m_browser->viewport()->update();
        return;
    }

    const QUrl url(u"https://danbooru.donmai.us/posts/%1.json"_s.arg(postId));
    QNetworkReply* reply = m_network->get(jsonRequest(url));

    connect(reply, &QNetworkReply::finished, this, [this, postId, resourceUrl, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) return;

        downloadThumbAndFade(doc.object()[u"preview_file_url"_s].toString(), resourceUrl,
                             [this, postId](const QPixmap& fitted) {
                                 m_postThumbs[postId] = fitted;
                             });
    });
}

void TagWikiPage::fetchAssetData(int assetId)
{
    const QString resourceUrl = u"asset:%1"_s.arg(assetId);

    const auto cached = m_assetThumbs.constFind(assetId);
    if (cached != m_assetThumbs.cend()) {
        m_browser->document()->addResource(QTextDocument::ImageResource, QUrl(resourceUrl),
                                           QVariant(cached.value()));
        m_browser->document()->markContentsDirty(0, m_browser->document()->characterCount());
        m_browser->viewport()->update();
        return;
    }

    const QUrl url(u"https://danbooru.donmai.us/media_assets/%1.json"_s.arg(assetId));
    QNetworkReply* reply = m_network->get(jsonRequest(url));

    connect(reply, &QNetworkReply::finished, this, [this, assetId, resourceUrl, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) return;

        // The smallest variant that exists; the first entry as a last resort.
        const QJsonArray variants = doc.object()[u"variants"_s].toArray();
        static const QStringList preferred = {u"180x180"_s, u"360x360"_s, u"720x720"_s,
                                              u"sample"_s, u"original"_s};

        QString thumbUrl;
        for (const QString& wanted : preferred) {
            for (const QJsonValue variant : variants) {
                if (variant.toObject()[u"type"_s].toString() != wanted) continue;
                thumbUrl = variant.toObject()[u"url"_s].toString();
                break;
            }
            if (!thumbUrl.isEmpty()) break;
        }
        if (thumbUrl.isEmpty() && !variants.isEmpty())
            thumbUrl = variants.first().toObject()[u"url"_s].toString();

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
        ->setText(u"No wiki page found for \"%1\"."_s.arg(tag));
    m_mainStack->setCurrentIndex(2);
    finishPendingFadeIn();
}

void TagWikiPage::displayContent(const QString& title, const QStringList& otherNames,
                                 const QString& body)
{
    // Fades and fetches from the previous article point at resource urls the
    // document about to be installed does not have.
    m_pendingFades.clear();
    m_thumbFadeTimer->stop();
    m_thumbFetchQueue.clear();
    m_thumbFetchTimer->stop();

    m_titleLabel->setText(title.isEmpty() ? m_currentTag : normalizeTag(title));

    if (otherNames.isEmpty()) {
        m_aliasLabel->hide();
    } else {
        m_aliasLabel->setText(u"Also known as: "_s + otherNames.join(u", "_s));
        m_aliasLabel->show();
    }

    QList<int> postIds;
    QList<int> assetIds;
    const QString html = buildDocument(body, postIds, assetIds);

    // A transparent placeholder stops the broken-image glyph flashing and
    // pins the layout for the eventual fade-in.
    QPixmap placeholder(kThumbWidth, kThumbHeight);
    placeholder.fill(Qt::transparent);

    for (int id : postIds)
        m_browser->document()->addResource(QTextDocument::ImageResource,
                                           QUrl(u"post:%1"_s.arg(id)),
                                           QVariant(m_postThumbs.value(id, placeholder)));
    for (int id : assetIds)
        m_browser->document()->addResource(QTextDocument::ImageResource,
                                           QUrl(u"asset:%1"_s.arg(id)),
                                           QVariant(m_assetThumbs.value(id, placeholder)));

    if (!m_fontApplied) {
        const QStringList families = QFontDatabase::applicationFontFamilies(0);
        if (!families.isEmpty()) {
            m_browser->setFont(QFont(families.first(), 15));
            m_fontApplied = true;
        }
    }

    // setHtml resets the scrollbar range, so an in-flight target is stale.
    if (m_scrollAnim->state() == QAbstractAnimation::Running) m_scrollAnim->stop();

    m_browser->setHtml(html);
    m_mainStack->setCurrentIndex(1);
    finishPendingFadeIn();

    for (int id : postIds)
        if (!m_postThumbs.contains(id)) enqueueThumbFetch(ThumbKind::Post, id);
    for (int id : assetIds)
        if (!m_assetThumbs.contains(id)) enqueueThumbFetch(ThumbKind::Asset, id);
}

bool TagWikiPage::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_browser && event->type() == QEvent::Resize) {
        // The document's lazy re-flow can leave image cells overlapping text
        // after a width change, so a full re-layout is forced.
        if (QTextDocument* doc = m_browser->document())
            doc->markContentsDirty(0, doc->characterCount());
    }

    if (watched != m_browser->viewport() || event->type() != QEvent::Wheel)
        return QWidget::eventFilter(watched, event);

    auto* wheel = static_cast<QWheelEvent*>(event);

    // A zoom modifier and a touchpad's pixel deltas both want the raw event.
    if (wheel->modifiers() != Qt::NoModifier) return false;
    if (!wheel->pixelDelta().isNull()) return false;

    const int notches = wheel->angleDelta().y() / 120;
    if (notches == 0) return false;

    // Based on the in-flight target, so fast notches stack rather than each
    // restarting from where the view happens to be.
    QScrollBar* bar = m_browser->verticalScrollBar();
    const int base = m_scrollAnim->state() == QAbstractAnimation::Running
        ? m_scrollAnim->endValue().toInt()
        : bar->value();

    smoothScrollTo(base - notches * 60);
    return true;
}

// ---- Thumbnail fades

void TagWikiPage::startThumbFade(const QString& resourceUrl, const QPixmap& finalPixmap)
{
    // A re-fetch of a url already fading just restarts its ramp.
    for (PendingThumbFade& fade : m_pendingFades) {
        if (fade.resourceUrl != resourceUrl) continue;
        fade.finalPixmap = finalPixmap;
        fade.step = 0;
        if (!m_thumbFadeTimer->isActive()) m_thumbFadeTimer->start();
        return;
    }

    m_pendingFades.append({resourceUrl, finalPixmap, 0});
    if (!m_thumbFadeTimer->isActive()) m_thumbFadeTimer->start();
}

void TagWikiPage::onThumbFadeTick()
{
    constexpr int totalMs = 220;
    constexpr int stepMs = 25;
    constexpr int totalSteps = totalMs / stepMs;

    bool any = false;
    for (qsizetype i = m_pendingFades.size() - 1; i >= 0; --i) {
        PendingThumbFade& fade = m_pendingFades[i];
        ++fade.step;

        QPixmap faded(fade.finalPixmap.size());
        faded.fill(Qt::transparent);
        {
            QPainter painter(&faded);
            painter.setOpacity(qMin(1.0f, float(fade.step) / float(totalSteps)));
            painter.drawPixmap(0, 0, fade.finalPixmap);
        }

        m_browser->document()->addResource(QTextDocument::ImageResource,
                                           QUrl(fade.resourceUrl), QVariant(faded));
        any = true;

        if (fade.step >= totalSteps) m_pendingFades.removeAt(i);
    }

    // One re-layout for the whole batch, however many thumbnails moved.
    if (any) {
        m_browser->document()->markContentsDirty(0, m_browser->document()->characterCount());
        m_browser->viewport()->update();
    }
    if (m_pendingFades.isEmpty()) m_thumbFadeTimer->stop();
}

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
    if (job.kind == ThumbKind::Post)
        fetchPostData(job.id);
    else
        fetchAssetData(job.id);
}

// ---- Page transitions

void TagWikiPage::startFadeOutThenLookup(const QString& tag)
{
    m_pendingTag = tag;
    m_pendingHistoryNav = false; // a link click overrides a pending history step

    m_fadeAnim->stop();
    m_fadeAnim->setStartValue(m_fadeEffect->opacity());
    m_fadeAnim->setEndValue(0.0);
    m_fadeAnim->start();
}

void TagWikiPage::startFadeOutThenHistory()
{
    m_pendingHistoryNav = true;
    m_pendingTag.clear(); // and a history step overrides a pending link

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

void TagWikiPage::onAnchorClicked(const QUrl& url)
{
    if (url.scheme() == "wiki"_L1) {
        startFadeOutThenLookup(url.path());
        return;
    }
    if (url.scheme() == "post"_L1) {
        QDesktopServices::openUrl(
            QUrl(u"https://danbooru.donmai.us/posts/%1"_s.arg(url.path())));
        return;
    }
    if (url.scheme() == "asset"_L1) {
        QDesktopServices::openUrl(
            QUrl(u"https://danbooru.donmai.us/media_assets/%1"_s.arg(url.path())));
        return;
    }

    // A same-page anchor, such as a table-of-contents jump. The href starts
    // with '#', but Qt resolves it against the document's empty source and can
    // leave a scheme on it, so this keys off the fragment rather than gating
    // on the scheme.
    if (!url.fragment().isEmpty()) {
        smoothScrollToAnchor(url.fragment());
        return;
    }

    QDesktopServices::openUrl(url);
}

QString TagWikiPage::buildDocument(const QString& dtext, QList<int>& outPostIds,
                                   QList<int>& outAssetIds) const
{
    // The markup conversion is shared with the facet editor's rail; only this
    // wrapper is page-specific.
    const QString body =
        dtextToHtml(dtext, &outPostIds, &outAssetIds, kThumbWidth, kThumbHeight);

    const QStringList families = QFontDatabase::applicationFontFamilies(0);
    const QString fontDeclaration =
        families.isEmpty() ? QString()
                           : u"font-family:'%1',sans-serif;"_s.arg(families.first());

    const QString css =
        u"<style>body{background:transparent;color:#c0c0c0;font-size:20px;margin:0;padding:0;"_s
        + fontDeclaration
        + u"}"
          "h1,h2,h3,h4,h5,h6{color:#888;border-bottom:1px solid #222;padding-bottom:3px;"
          "margin-top:14px;}"
          "a{color:#66aa66;text-decoration:none;}"
          "blockquote{border-left:2px solid #333;margin:4px 0 4px 8px;padding-left:10px;"
          "color:#888;}"
          "code{background:#1a1a1a;border-radius:3px;padding:1px 4px;font-family:monospace;"
          "font-size:12px;}"
          "ul,ol{padding-left:20px;margin:4px 0;}"
          "li{margin:2px 0;}"
          "table.gallery{margin:6px 0;}"
          "td.thumb{color:#888;font-size:20px;}"
          ".ws{margin:6px 0;}"
          ".wsh{color:#666;font-weight:bold;font-size:11px;margin:0 0 3px 0;}"
          ".wtn{color:#666;font-size:11px;}"
          ".wsp{color:#555;}"
          "</style>"_s;

    // The body is centred by a three-cell table: QTextBrowser has no usable
    // max-width, so a fixed middle column is what keeps long lines readable.
    return u"<html><head>"_s + css
        + u"</head><body><table width='100%' cellspacing='0' cellpadding='0'><tr><td></td>"
          "<td width='800' style='padding:16px 8px;'>"_s
        + body + u"</td><td></td></tr></table></body></html>"_s;
}

} // namespace tc
