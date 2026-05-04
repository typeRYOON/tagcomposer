#include <gui/tagwikipage.h>
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
#include <QDebug>
#include <QPixmap>
#include <QPainter>
#include <QTimer>
#include <QEvent>
#include <QShowEvent>
#include <QWindow>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>


namespace gui {

static constexpr int ThumbW = 150;
static constexpr int ThumbH = 150;


// ── ctor ──────────────────────────────────────────────────────────────────────

TagWikiPage::TagWikiPage(QWidget* parent) : QWidget(parent), m_nam(new QNetworkAccessManager(this))
{
    setObjectName("TagWikiPage");
    setAttribute(Qt::WA_StyledBackground, true);

    // ── Header ────────────────────────────────────────────────────────────────
    m_titleLabel = new QLabel(this);
    m_titleLabel->setAttribute(Qt::WA_StyledBackground, true);
    m_titleLabel->setObjectName("WikiTitle");
    m_titleLabel->setWordWrap(true);

    m_aliasLabel = new QLabel(this);
    m_aliasLabel->setAttribute(Qt::WA_StyledBackground, true);
    m_aliasLabel->setObjectName("WikiAlias");
    m_aliasLabel->setWordWrap(true);
    m_aliasLabel->hide();

    // Inner column constrained to same max-width as the browser content
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

    // ── Text browser ──────────────────────────────────────────────────────────
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

    // ── Content widget ────────────────────────────────────────────────────────
    auto* contentWidget = new QWidget;
    auto* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(header);
    contentLayout->addWidget(m_browser, 1);

    // ── Loading / not-found placeholders ─────────────────────────────────────
    // Loading state is intentionally empty - the search bar above is enough
    // of a hint that the user types a tag in.
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

    // Cross-fade effect for wiki-link transitions. The graphics effect is
    // attached to the stack so all three children (loading / content /
    // not-found) share one opacity. Single permanent finished-handler reads
    // m_pendingTag / m_pendingFadeIn to decide what happens at end-of-anim.
    m_fadeEffect = new QGraphicsOpacityEffect(m_mainStack);
    m_fadeEffect->setOpacity(1.0);
    m_mainStack->setGraphicsEffect(m_fadeEffect);
    m_fadeAnim = new QPropertyAnimation(m_fadeEffect, "opacity", this);
    m_fadeAnim->setDuration(180);
    m_fadeAnim->setEasingCurve(QEasingCurve::InOutSine);
    connect(m_fadeAnim, &QPropertyAnimation::finished, this, [this]() {
        // Animation is in fade-out direction when endValue is 0; that's our
        // signal to actually request the new page. The fade-in path runs
        // from displayContent so we don't need to do anything on its finish.
        if (m_fadeAnim->endValue().toReal() < 0.5 && !m_pendingTag.isEmpty()) {
            const QString tag = m_pendingTag;
            m_pendingTag.clear();
            m_pendingFadeIn = true;
            emit wikiLinkClicked(tag);
        }
    });

    m_searchBar = new TagSearchBar(this);
    // Search-bar commits go through the same fade-out / fade-in path as
    // in-document wiki links so the page transition feels consistent.
    connect(m_searchBar, &TagSearchBar::tagAdded, this, &TagWikiPage::startFadeOutThenLookup);

    // Top bar: [back] [forward] [search bar] [open in browser]. The two
    // history buttons sit to the left of the search bar; open-in-browser
    // hangs off the right. WikiNavBtn is a transparent flat-button style
    // already in wiki.qss, with proper disabled-state colour for the
    // history-bounds case.
    m_backBtn = new QPushButton("←"); // ←
    m_backBtn->setObjectName("WikiNavBtn");
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setToolTip("Back (Alt+Left)");
    connect(m_backBtn, &QPushButton::clicked, this, &TagWikiPage::goBack);

    m_forwardBtn = new QPushButton("→"); // →
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

    // Browser-style keybindings. Ctrl+Z used to be wired here but the
    // search-bar's QLineEdit consumes it for text-undo when focused, so
    // the navigation never fired. Alt+Left / Alt+Right have no QLineEdit
    // conflict and match what users expect from web browsers.
    auto* backShortcut = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Left), this);
    backShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(backShortcut, &QShortcut::activated, this, &TagWikiPage::goBack);

    auto* fwdShortcut = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Right), this);
    fwdShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(fwdShortcut, &QShortcut::activated, this, &TagWikiPage::goForward);

    updateNavButtons();
}

// ── Public API ────────────────────────────────────────────────────────────────

void TagWikiPage::setDanbooruIndex(core::DanbooruIndex* index)
{
    m_searchBar->setIndex(index);
}

void TagWikiPage::lookupTag(const QString& tag)
{
    if (tag == m_currentTag && m_mainStack->currentIndex() == 1) {
        // No-op early exit. If a fade-out already ran (e.g. user committed
        // the same tag from the search bar), restore opacity so the page
        // doesn't get stuck blank.
        finishPendingFadeIn();
        return;
    }

    // Push to history unless we're already navigating through it
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

// ── History navigation ────────────────────────────────────────────────────────

void TagWikiPage::goBack()
{
    if (m_historyPos <= 0) return;
    cancelPendingFade();
    --m_historyPos;
    loadFromHistory();
}

void TagWikiPage::goForward()
{
    if (m_historyPos >= m_history.size() - 1) return;
    cancelPendingFade();
    ++m_historyPos;
    loadFromHistory();
}

void TagWikiPage::loadFromHistory()
{
    m_navigating = true;
    lookupTag(m_history[m_historyPos]);
    m_navigating = false;
}

void TagWikiPage::cancelPendingFade()
{
    // History navigation sidesteps the wiki-link crossfade. If a fade-out
    // is mid-flight (user clicked an in-document [[wiki link]] then hit
    // Back before the 180ms fade landed), the queued m_pendingTag would
    // otherwise hijack the navigation when the fade-finished handler fires.
    // Stop the animation, drop the queue, and snap opacity back to 1.0.
    m_fadeAnim->stop();
    m_pendingTag.clear();
    m_pendingFadeIn = false;
    m_fadeEffect->setOpacity(1.0);
}

// ── Network ───────────────────────────────────────────────────────────────────

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

                // Aspect-ratio-preserving fit into a ThumbW x ThumbH box, then
                // compose onto a transparent ThumbW x ThumbH canvas so every
                // thumbnail is exactly the same logical size. The HTML img tag
                // sets width='150' height='150', so without this padding step
                // non-square thumbs would get stretched to fill the cell.
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

        // media_assets returns a `variants` array with entries like
        // {"type": "180x180", "url": "...", ...}. Pick the smallest variant
        // we can find; fall back to whatever's first if Danbooru ever
        // changes the catalogue.
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

// ── Display ───────────────────────────────────────────────────────────────────

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
    // Debug: dump raw DText body so layout / table-of-contents bugs can be
    // reproduced from the exact source markup. Surrounded with markers so the
    // multi-line content is easy to copy/paste out of the debug stream.
    qDebug().noquote().nospace() << "\n=== TagWikiPage body for tag \"" << m_currentTag
                                 << "\" (title=\"" << title << "\") ===\n"
                                 << body << "\n=== end TagWikiPage body ===";

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

    // Register a resource for every <kind>:<id> referenced in the HTML.
    // Cached thumbs get the real pixmap immediately. Non-cached ones get a
    // fully transparent placeholder of the same dimensions: this prevents
    // the broken-image icon from flashing before the network fetch lands,
    // and pins the cell size so layout stays stable when the real thumbnail
    // fades in later.
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

    m_browser->setHtml(html);
    m_mainStack->setCurrentIndex(1);
    finishPendingFadeIn();

    for (int id : postIds)
        if (!m_postThumbs.contains(id)) fetchPostData(id);
    for (int id : assetIds)
        if (!m_assetThumbs.contains(id)) fetchAssetData(id);
}

// ── Resize / screen-change re-layout ─────────────────────────────────────────

bool TagWikiPage::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_browser && event->type() == QEvent::Resize) {
        // QTextDocument's automatic re-flow on viewport resize sometimes
        // leaves text overlapping image cells, most visibly when the window
        // is dragged to a larger monitor. Marking the whole document dirty
        // forces a full layout pass after the new width is applied.
        if (auto* doc = m_browser->document()) doc->markContentsDirty(0, doc->characterCount());
    }
    return QWidget::eventFilter(obj, event);
}

void TagWikiPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    // Hook the top-level window's screenChanged signal exactly once. Moving
    // a window between monitors that share the same logical size but differ
    // in device-pixel ratio doesn't fire a Resize event on the browser - the
    // text re-rasterises automatically but cached image-cell metrics from
    // the previous DPR are kept, so images end up oversized relative to text
    // until something forces a re-layout. screenChanged is the right signal
    // for that case; markContentsDirty here matches what the resize filter
    // does for window-size changes.
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

// ── Thumbnail fade ────────────────────────────────────────────────────────────

void TagWikiPage::startThumbFade(const QString& resourceUrl, const QPixmap& finalPix)
{
    constexpr int totalMs = 220;
    constexpr int stepMs = 25;
    constexpr int totalSteps = totalMs / stepMs;

    auto* timer = new QTimer(this);
    timer->setInterval(stepMs);
    int step = 0;
    connect(timer, &QTimer::timeout, this, [this, timer, step, resourceUrl, finalPix]() mutable {
        ++step;
        const float alpha = qMin(1.0f, float(step) / float(totalSteps));

        QPixmap faded(finalPix.size());
        faded.fill(Qt::transparent);
        QPainter p(&faded);
        p.setOpacity(alpha);
        p.drawPixmap(0, 0, finalPix);
        p.end();

        m_browser->document()->addResource(QTextDocument::ImageResource, QUrl(resourceUrl),
                                           QVariant(faded));
        m_browser->document()->markContentsDirty(0, m_browser->document()->characterCount());
        m_browser->viewport()->update();

        if (step >= totalSteps) {
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start();
}

// ── Nav button state ─────────────────────────────────────────────────────────

void TagWikiPage::updateNavButtons()
{
    if (m_backBtn) m_backBtn->setEnabled(m_historyPos > 0);
    if (m_forwardBtn) m_forwardBtn->setEnabled(m_historyPos < m_history.size() - 1);
    if (m_openExternalBtn) m_openExternalBtn->setEnabled(!m_currentTag.isEmpty());
}

// ── Page transition helpers ──────────────────────────────────────────────────

void TagWikiPage::startFadeOutThenLookup(const QString& tag)
{
    m_pendingTag = tag;
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

// ── Anchor clicks ─────────────────────────────────────────────────────────────

void TagWikiPage::onAnchorClicked(const QUrl& url)
{
    if (url.scheme() == "wiki") {
        startFadeOutThenLookup(url.path());
    }
    else if (url.scheme() == "post") {
        QDesktopServices::openUrl(
            QUrl(QString("https://danbooru.donmai.us/posts/%1").arg(url.path())));
    }
    else if (url.scheme() == "asset") {
        QDesktopServices::openUrl(
            QUrl(QString("https://danbooru.donmai.us/media_assets/%1").arg(url.path())));
    }
    else if (url.scheme().isEmpty() && !url.fragment().isEmpty()) {
        // Same-page anchor link (e.g. table-of-contents jumps). The href
        // is `#dtext-intro`, parsed by QUrl into an empty scheme and a
        // fragment - hand it to scrollToAnchor instead of openUrl, which
        // would try to launch an external handler for a bare fragment.
        m_browser->scrollToAnchor(url.fragment());
    }
    else {
        QDesktopServices::openUrl(url);
    }
}

// ── DText → HTML ─────────────────────────────────────────────────────────────

static QString applyInlineMarkup(const QString& raw)
{
    QString s = raw.toHtmlEscaped();
    s.replace(QRegularExpression(R"(\[b\](.*?)\[/b\])"), "<b>\\1</b>");
    s.replace(QRegularExpression(R"(\[i\](.*?)\[/i\])"), "<i>\\1</i>");
    s.replace(QRegularExpression(R"(\[u\](.*?)\[/u\])"), "<u>\\1</u>");
    s.replace(QRegularExpression(R"(\[s\](.*?)\[/s\])"), "<s>\\1</s>");
    return s;
}

QString TagWikiPage::dtextToHtml(const QString& dtext, QList<int>& outPostIds,
                                 QList<int>& outAssetIds)
{
    QString text = dtext;

    // 0. Normalize line endings (Danbooru API returns \r\n)
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');

    // 1. Extract !post / !asset bullet IDs for image fetching. Each kind
    //    capped at 20 entries so a runaway page doesn't queue hundreds of
    //    fetches before the user has a chance to navigate away.
    {
        static const QRegularExpression mediaBulletExtractRe(R"(^\*+[ \t]+!(post|asset) #(\d+))",
                                                             QRegularExpression::MultilineOption);
        auto it = mediaBulletExtractRe.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            const QString kind = m.captured(1);
            const int id = m.captured(2).toInt();
            if (kind == "post") {
                if (!outPostIds.contains(id) && outPostIds.size() < 20) outPostIds << id;
            }
            else { // "asset"
                if (!outAssetIds.contains(id) && outAssetIds.size() < 20) outAssetIds << id;
            }
        }
    }

    // 2. Strip {{...}} tag-search embeds
    text.remove(QRegularExpression(R"(\{\{[^}]*\}\})"));

    // 3. Pre-tokenize external links before HTML-escaping quotes.
    //    Handles both absolute (https?://) and Danbooru-relative (/pools/, /posts/, etc.) URLs.
    //    If a parenthetical description follows the link  "title":url (desc)  it is used as the
    //    display text instead of the title; this matches how Danbooru renders pool/search links.
    //    Inline DText markup inside titles (e.g. [i]...[/i]) is processed here so it renders
    //    correctly after HTML-escaping.
    QHash<QString, QPair<QString, QString>> linkTokens; // token → {displayHtml, resolvedUrl}
    {
        // Group 1: link title (may contain DText markup)
        // Group 2: URL - absolute https?://, Danbooru-relative /, or same-page #anchor
        // Group 3: optional following parenthetical text (without the outer parens)
        static const QRegularExpression extRe(
            R"~("([^"]+)":\[?((?:https?://|/|#)[^\]\s"]+)\]?(?:\s*\(([^)]*)\))?)~");

        int n = 0;
        int offset = 0;
        while (true) {
            const QRegularExpressionMatch m = extRe.match(text, offset);
            if (!m.hasMatch()) break;

            const QString rawTitle = m.captured(1);
            const QString rawUrl = m.captured(2);
            const QString rawParen = m.captured(3); // empty if no parenthetical

            // Resolve relative URLs to danbooru.donmai.us
            const QString resolvedUrl =
                rawUrl.startsWith('/') ? "https://danbooru.donmai.us" + rawUrl : rawUrl;

            // Display text: parenthetical if present (pool/search links), else title.
            // Process DText inline markup in the title so [i]...[/i] renders correctly.
            const QString displayHtml =
                rawParen.isEmpty() ? applyInlineMarkup(rawTitle) : rawParen.toHtmlEscaped();

            const QString token = QString("__LNKTOK%1__").arg(n++);
            linkTokens[token] = {displayHtml, resolvedUrl};
            text.replace(m.capturedStart(), m.capturedLength(), token);
            offset = m.capturedStart() + token.size();
        }
    }

    // 4. HTML-escape
    text = text.toHtmlEscaped();

    // 5. Block elements
    static const QRegularExpression sectionRe(
        R"(\[section(?:,expanded)?=([^\]]*)\](.*?)\[/section\])",
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    text.replace(sectionRe, "<div class='ws'><p class='wsh'>\\1</p>\\2</div>");

    // [expand=Title]...[/expand] (or bare [expand]...[/expand]) - DText's
    // collapsible block. QTextBrowser can't actually collapse, so render the
    // same way as [section]: titled box, contents inline. Title group is
    // optional so the bare form doesn't fall through unmatched.
    static const QRegularExpression expandRe(R"(\[expand(?:=([^\]]*))?\](.*?)\[/expand\])",
                                             QRegularExpression::DotMatchesEverythingOption |
                                                 QRegularExpression::CaseInsensitiveOption);
    text.replace(expandRe, "<div class='ws'><p class='wsh'>\\1</p>\\2</div>");

    text.replace(QRegularExpression(R"(\[quote\](.*?)\[/quote\])",
                                    QRegularExpression::DotMatchesEverythingOption |
                                        QRegularExpression::CaseInsensitiveOption),
                 "<blockquote>\\1</blockquote>");

    text.replace(QRegularExpression(R"(\[spoiler(?:s)?\](.*?)\[/spoiler(?:s)?\])",
                                    QRegularExpression::DotMatchesEverythingOption |
                                        QRegularExpression::CaseInsensitiveOption),
                 "<span class='wsp'>\\1</span>");

    text.replace(
        QRegularExpression(R"(\[tn\](.*?)\[/tn\])", QRegularExpression::DotMatchesEverythingOption |
                                                        QRegularExpression::CaseInsensitiveOption),
        "<small class='wtn'>\\1</small>");

    text.replace(QRegularExpression(R"(\[code\](.*?)\[/code\])",
                                    QRegularExpression::DotMatchesEverythingOption |
                                        QRegularExpression::CaseInsensitiveOption),
                 "<code>\\1</code>");

    // 6. Inline formatting
    text.replace(
        QRegularExpression(R"(\[b\](.*?)\[/b\])", QRegularExpression::DotMatchesEverythingOption),
        "<b>\\1</b>");
    text.replace(
        QRegularExpression(R"(\[i\](.*?)\[/i\])", QRegularExpression::DotMatchesEverythingOption),
        "<i>\\1</i>");
    text.replace(
        QRegularExpression(R"(\[u\](.*?)\[/u\])", QRegularExpression::DotMatchesEverythingOption),
        "<u>\\1</u>");
    text.replace(
        QRegularExpression(R"(\[s\](.*?)\[/s\])", QRegularExpression::DotMatchesEverythingOption),
        "<s>\\1</s>");
    text.remove(
        QRegularExpression(R"(\[color=[^\]]*\])", QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression(R"(\[/color\])", QRegularExpression::CaseInsensitiveOption));

    // 7. Headers (h1. - h6. at start of line). Two passes per level: the
    // anchored form `h5#name. Title` runs first (more specific) so the simple
    // regex doesn't grab it. Anchored headings emit a paired <a name='...'>
    // marker so QTextBrowser::scrollToAnchor lands on them when a same-page
    // link like `"Intro":#dtext-intro` is clicked.
    for (int n = 6; n >= 1; --n) {
        text.replace(QRegularExpression(QString("^h%1#([\\w-]+)\\.[ \\t]*(.+)$").arg(n),
                                        QRegularExpression::MultilineOption),
                     QString("<h%1><a name='\\1'></a>\\2</h%1>").arg(n));
        text.replace(QRegularExpression(QString("^h%1\\.[ \\t]*(.+)$").arg(n),
                                        QRegularExpression::MultilineOption),
                     QString("<h%1>\\1</h%1>").arg(n));
    }

    // 8. Wiki links  [[tag|display]] then [[tag|]] (empty alias) then [[tag]]
    // Use opaque URI "wiki:TAG" - tag lands in url.path(), not url.host(),
    // so underscores and parens in tag names are handled correctly.
    text.replace(QRegularExpression(R"(\[\[([^\|\]]+)\|([^\]]+)\]\])"),
                 "<a href='wiki:\\1'>\\2</a>");
    text.replace(QRegularExpression(R"(\[\[([^\|\]]+)\|?\]\])"), "<a href='wiki:\\1'>\\1</a>");

    // 8.5. Convert !post / !asset bullet groups into inline image galleries.
    // Runs after step 8 so wiki-link captions are already converted to <a>
    // tags. Both kinds use the same gallery layout; only the URL scheme and
    // the default caption prefix differ. Mixed runs (e.g. !post then !asset
    // back-to-back) flow into one table since they share the same row width
    // budget - the cells already carry their own kind in the href.
    {
        static const QRegularExpression mediaBulletRe(
            "^\\*+[ \\t]+!(post|asset) #(\\d+)(?::\\s*(.*))?$");

        struct GalleryItem {
            QString kind;
            int id;
            QString caption;
        };
        QStringList lines = text.split('\n');
        QStringList out;
        QList<GalleryItem> gallery;

        auto flushGallery = [&]() {
            if (gallery.isEmpty()) return;
            const int maxPerRow = qMax(1, 800 / (ThumbW + 10));
            out << "<table class='gallery' cellspacing='0' cellpadding='0'>";
            for (int i = 0; i < gallery.size(); ++i) {
                if (i % maxPerRow == 0) {
                    if (i > 0) out << "</tr>";
                    out << "<tr>";
                }
                const GalleryItem& g = gallery[i];
                const QString cap =
                    g.caption.isEmpty() ? QString("%1 #%2").arg(g.kind).arg(g.id) : g.caption;
                // width/height on the <img> lock the cell to ThumbW x ThumbH
                // *logical* pixels. Without these attrs the document uses the
                // pixmap's pixel dimensions for layout, which means dragging
                // the window to a screen with a different device-pixel ratio
                // can scale image cells without scaling the surrounding text.
                // The href / src share `<kind>:<id>`; onAnchorClicked routes
                // the click to /posts/ or /media_assets/ accordingly, and
                // the resource is registered under the same URL.
                out << QString("<td class='thumb' align='center' style='padding:0 10px 4px "
                               "0;vertical-align:middle;width:%3px;'>"
                               "<a href='%1:%2'><img src='%1:%2' width='%3' height='%5'></a>"
                               "<br><small>%4</small></td>")
                           .arg(g.kind)
                           .arg(g.id)
                           .arg(ThumbW)
                           .arg(cap)
                           .arg(ThumbH);
            }
            out << "</tr></table>";
            gallery.clear();
        };

        for (const QString& line : lines) {
            const QRegularExpressionMatch m = mediaBulletRe.match(line);
            if (m.hasMatch()) {
                gallery.append({m.captured(1), m.captured(2).toInt(), m.captured(3).trimmed()});
            }
            else {
                flushGallery();
                out << line;
            }
        }
        flushGallery();
        text = out.join('\n');
    }

    // 9. Remaining standalone "post #N" references → clickable links
    text.replace(QRegularExpression(R"(\bpost #(\d+))"),
                 "<a href='https://danbooru.donmai.us/posts/\\1'>post #\\1</a>");

    // 10. Lists (line-by-line). The leading `*` / `#` count is the nesting
    // depth, so `* foo` / `** bar` / `*** baz` produce a nested <ul> tree
    // rather than three flat siblings. Switching between unordered and
    // ordered closes the open list type completely before opening the other.
    {
        static const QRegularExpression ulRe("^(\\*+)[ \\t]+(.+)$");
        static const QRegularExpression olRe("^(#+)[ \\t]+(.+)$");
        QStringList lines = text.split('\n');
        QStringList out;
        int ulDepth = 0;
        int olDepth = 0;
        auto closeAll = [&]() {
            while (ulDepth > 0) {
                out << "</ul>";
                --ulDepth;
            }
            while (olDepth > 0) {
                out << "</ol>";
                --olDepth;
            }
        };
        for (const QString& line : lines) {
            const auto ulM = ulRe.match(line);
            const auto olM = olRe.match(line);
            if (ulM.hasMatch()) {
                while (olDepth > 0) {
                    out << "</ol>";
                    --olDepth;
                }
                const int target = ulM.captured(1).size();
                while (ulDepth < target) {
                    out << "<ul>";
                    ++ulDepth;
                }
                while (ulDepth > target) {
                    out << "</ul>";
                    --ulDepth;
                }
                out << "<li>" + ulM.captured(2) + "</li>";
            }
            else if (olM.hasMatch()) {
                while (ulDepth > 0) {
                    out << "</ul>";
                    --ulDepth;
                }
                const int target = olM.captured(1).size();
                while (olDepth < target) {
                    out << "<ol>";
                    ++olDepth;
                }
                while (olDepth > target) {
                    out << "</ol>";
                    --olDepth;
                }
                out << "<li>" + olM.captured(2) + "</li>";
            }
            else {
                closeAll();
                out << line;
            }
        }
        closeAll();
        text = out.join('\n');
    }

    // 11. Restore external link tokens (display text is pre-processed HTML from step 3)
    for (auto it = linkTokens.cbegin(); it != linkTokens.cend(); ++it) {
        text.replace(it.key(),
                     QString("<a href='%1'>%2</a>").arg(it.value().second, it.value().first));
    }

    // 12. Smart paragraph / line-break reconstruction.
    // Block-level elements must not be wrapped in <p>.
    {
        static const QRegularExpression blockElemRe(
            "^\\s*</?(?:h[1-6]|ul|ol|li|table|tr|td|blockquote|div)[\\s>/]",
            QRegularExpression::CaseInsensitiveOption);

        const QStringList paras = text.split(QRegularExpression("\\n{2,}"));
        QStringList result;
        for (QString para : paras) {
            para = para.trimmed();
            if (para.isEmpty()) continue;
            if (blockElemRe.match(para).hasMatch()) {
                para.replace('\n', "");
                result << para;
            }
            else {
                para.replace('\n', "<br/>");
                result << "<p>" + para + "</p>";
            }
        }
        text = result.join('\n');
    }

    // Font from the application's loaded custom font (ID 0 in QFontDatabase)
    const QStringList fontFamilies = QFontDatabase::applicationFontFamilies(0);
    const QString fontDecl =
        fontFamilies.isEmpty() ? QString()
                               : QString("font-family:'%1',sans-serif;").arg(fontFamilies.first());

    const QString css =
        "<style>"
        "body{background:transparent;color:#c0c0c0;font-size:15px;margin:0;padding:0;" +
        fontDecl +
        "}"
        "h1,h2,h3,h4,h5,h6{color:#888;border-bottom:1px solid "
        "#222;padding-bottom:3px;margin-top:14px;}"
        "a{color:#5599cc;text-decoration:none;}"
        "blockquote{border-left:2px solid #333;margin:4px 0 4px 8px;padding-left:10px;color:#888;}"
        "code{background:#1a1a1a;border-radius:3px;padding:1px "
        "4px;font-family:monospace;font-size:12px;}"
        "ul,ol{padding-left:20px;margin:4px 0;}"
        "li{margin:2px 0;}"
        "table.gallery{margin:6px 0;}"
        "td.thumb{color:#888;font-size:11px;}"
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
