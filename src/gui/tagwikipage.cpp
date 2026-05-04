#include <gui/tagwikipage.h>
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


namespace gui {

static constexpr int ThumbW = 150;
static constexpr int ThumbH = 150;


// ── ctor ──────────────────────────────────────────────────────────────────────

TagWikiPage::TagWikiPage(QWidget* parent)
    : QWidget(parent)
    , m_nam(new QNetworkAccessManager(this))
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
    connect(m_browser, &QTextBrowser::anchorClicked,
            this,      &TagWikiPage::onAnchorClicked);

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

    m_searchBar = new TagSearchBar(this);
    connect(m_searchBar, &TagSearchBar::tagAdded,
            this, &TagWikiPage::lookupTag);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(m_searchBar);
    root->addWidget(m_mainStack, 1);

    auto* backShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z), this);
    backShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(backShortcut, &QShortcut::activated, this, &TagWikiPage::goBack);

    auto* fwdShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), this);
    fwdShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(fwdShortcut, &QShortcut::activated, this, &TagWikiPage::goForward);
}

// ── Public API ────────────────────────────────────────────────────────────────

void TagWikiPage::setDanbooruIndex(core::DanbooruIndex* index)
{
    m_searchBar->setIndex(index);
}

void TagWikiPage::lookupTag(const QString& tag)
{
    if (tag == m_currentTag && m_mainStack->currentIndex() == 1)
        return;

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
    --m_historyPos;
    loadFromHistory();
}

void TagWikiPage::goForward()
{
    if (m_historyPos >= m_history.size() - 1) return;
    ++m_historyPos;
    loadFromHistory();
}

void TagWikiPage::loadFromHistory()
{
    m_navigating = true;
    lookupTag(m_history[m_historyPos]);
    m_navigating = false;
}

// ── Network ───────────────────────────────────────────────────────────────────

void TagWikiPage::fetchWikiPage(const QString& tag)
{
    const QByteArray encoded = QUrl::toPercentEncoding(tag);
    QUrl url(QString("https://danbooru.donmai.us/wiki_pages/%1.json")
                 .arg(QString::fromLatin1(encoded)));

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
                qobject_cast<QLabel*>(m_mainStack->widget(2))->setText(
                    "Network error: " + reply->errorString());
                m_mainStack->setCurrentIndex(2);
            }
            return;
        }

        const QByteArray data = reply->readAll();
        m_wikiCache[tag] = data;

        if (tag != m_currentTag) return;

        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) { showNotFound(tag); return; }

        QJsonObject obj = doc.object();
        QStringList others;
        for (const auto& v : obj["other_names"].toArray())
            others << v.toString();
        displayContent(obj["title"].toString(), others, obj["body"].toString());
    });
}

void TagWikiPage::fetchPostData(int postId)
{
    if (m_thumbCache.contains(postId)) {
        const QUrl imgSrc(QString("post:%1").arg(postId));
        m_browser->document()->addResource(
            QTextDocument::ImageResource, imgSrc, QVariant(m_thumbCache[postId]));
        m_browser->document()->markContentsDirty(0, m_browser->document()->characterCount());
        m_browser->viewport()->update();
        return;
    }

    QNetworkRequest req(QUrl(QString("https://danbooru.donmai.us/posts/%1.json").arg(postId)));
    req.setHeader(QNetworkRequest::UserAgentHeader, "TagComposer/1.0");
    req.setRawHeader("Accept", "application/json");

    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, postId, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) return;

        const QString previewUrl = doc.object()["preview_file_url"].toString();
        if (previewUrl.isEmpty()) return;

        const QUrl imgUrl(previewUrl);
        QNetworkRequest imgReq(imgUrl);
        imgReq.setHeader(QNetworkRequest::UserAgentHeader, "TagComposer/1.0");
        auto* imgReply = m_nam->get(imgReq);
        connect(imgReply, &QNetworkReply::finished, this, [this, postId, imgReply]() {
            imgReply->deleteLater();
            if (imgReply->error() != QNetworkReply::NoError) return;

            QPixmap pix;
            if (!pix.loadFromData(imgReply->readAll()) || pix.isNull()) return;

            const QPixmap scaled = pix.scaled(ThumbW, ThumbH,
                                               Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation);
            m_thumbCache[postId] = scaled;

            const QUrl imgSrc(QString("post:%1").arg(postId));
            m_browser->document()->addResource(
                QTextDocument::ImageResource, imgSrc, QVariant(scaled));
            m_browser->document()->markContentsDirty(
                0, m_browser->document()->characterCount());
            m_browser->viewport()->update();
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
    qobject_cast<QLabel*>(m_mainStack->widget(2))->setText(
        QString("No wiki page found for \"%1\".").arg(tag));
    m_mainStack->setCurrentIndex(2);
}

void TagWikiPage::displayContent(const QString& title,
                                 const QStringList& otherNames,
                                 const QString& body)
{
    // Debug: dump raw DText body so layout / table-of-contents bugs can be
    // reproduced from the exact source markup. Surrounded with markers so the
    // multi-line content is easy to copy/paste out of the debug stream.
    qDebug().noquote().nospace()
        << "\n=== TagWikiPage body for tag \"" << m_currentTag
        << "\" (title=\"" << title << "\") ===\n"
        << body
        << "\n=== end TagWikiPage body ===";

    m_titleLabel->setText(title.isEmpty() ? m_currentTag : utils::normalizeTagInput(title));

    if (!otherNames.isEmpty()) {
        m_aliasLabel->setText("Also known as: " + otherNames.join(", "));
        m_aliasLabel->show();
    } else {
        m_aliasLabel->hide();
    }

    QList<int> postIds;
    const QString html = dtextToHtml(body, postIds);

    for (int id : postIds) {
        if (m_thumbCache.contains(id)) {
            m_browser->document()->addResource(
                QTextDocument::ImageResource,
                QUrl(QString("post:%1").arg(id)),
                QVariant(m_thumbCache[id]));
        }
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

    for (int id : postIds)
        if (!m_thumbCache.contains(id))
            fetchPostData(id);
}

// ── Anchor clicks ─────────────────────────────────────────────────────────────

void TagWikiPage::onAnchorClicked(const QUrl& url)
{
    if (url.scheme() == "wiki") {
        emit wikiLinkClicked(url.path());
    } else if (url.scheme() == "post") {
        QDesktopServices::openUrl(
            QUrl(QString("https://danbooru.donmai.us/posts/%1").arg(url.path())));
    } else {
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

QString TagWikiPage::dtextToHtml(const QString& dtext, QList<int>& outPostIds)
{
    QString text = dtext;

    // 0. Normalize line endings (Danbooru API returns \r\n)
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');

    // 1. Extract !post bullet IDs for image fetching (limit 20)
    {
        static const QRegularExpression postBulletExtractRe(
            R"(^\*+[ \t]+!post #(\d+))", QRegularExpression::MultilineOption);
        auto it = postBulletExtractRe.globalMatch(text);
        while (it.hasNext()) {
            const int id = it.next().captured(1).toInt();
            if (!outPostIds.contains(id) && outPostIds.size() < 20)
                outPostIds << id;
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
    QHash<QString, QPair<QString,QString>> linkTokens; // token → {displayHtml, resolvedUrl}
    {
        // Group 1: link title (may contain DText markup)
        // Group 2: URL (absolute https?:// or Danbooru-relative /)
        // Group 3: optional following parenthetical text (without the outer parens)
        static const QRegularExpression extRe(
            R"~("([^"]+)":\[?((?:https?://|/)[^\]\s"]+)\]?(?:\s*\(([^)]*)\))?)~");

        int n = 0;
        int offset = 0;
        while (true) {
            const QRegularExpressionMatch m = extRe.match(text, offset);
            if (!m.hasMatch()) break;

            const QString rawTitle = m.captured(1);
            const QString rawUrl   = m.captured(2);
            const QString rawParen = m.captured(3); // empty if no parenthetical

            // Resolve relative URLs to danbooru.donmai.us
            const QString resolvedUrl = rawUrl.startsWith('/')
                ? "https://danbooru.donmai.us" + rawUrl
                : rawUrl;

            // Display text: parenthetical if present (pool/search links), else title.
            // Process DText inline markup in the title so [i]...[/i] renders correctly.
            const QString displayHtml = rawParen.isEmpty()
                ? applyInlineMarkup(rawTitle)
                : rawParen.toHtmlEscaped();

            const QString token = QString("__LNKTOK%1__").arg(n++);
            linkTokens[token] = { displayHtml, resolvedUrl };
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

    text.replace(
        QRegularExpression(R"(\[quote\](.*?)\[/quote\])",
            QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption),
        "<blockquote>\\1</blockquote>");

    text.replace(
        QRegularExpression(R"(\[spoiler(?:s)?\](.*?)\[/spoiler(?:s)?\])",
            QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption),
        "<span class='wsp'>\\1</span>");

    text.replace(
        QRegularExpression(R"(\[tn\](.*?)\[/tn\])",
            QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption),
        "<small class='wtn'>\\1</small>");

    text.replace(
        QRegularExpression(R"(\[code\](.*?)\[/code\])",
            QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption),
        "<code>\\1</code>");

    // 6. Inline formatting
    text.replace(QRegularExpression(R"(\[b\](.*?)\[/b\])",  QRegularExpression::DotMatchesEverythingOption), "<b>\\1</b>");
    text.replace(QRegularExpression(R"(\[i\](.*?)\[/i\])",  QRegularExpression::DotMatchesEverythingOption), "<i>\\1</i>");
    text.replace(QRegularExpression(R"(\[u\](.*?)\[/u\])",  QRegularExpression::DotMatchesEverythingOption), "<u>\\1</u>");
    text.replace(QRegularExpression(R"(\[s\](.*?)\[/s\])",  QRegularExpression::DotMatchesEverythingOption), "<s>\\1</s>");
    text.remove(QRegularExpression(R"(\[color=[^\]]*\])",   QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression(R"(\[/color\])",         QRegularExpression::CaseInsensitiveOption));

    // 7. Headers (h1.–h6. at start of line)
    for (int n = 6; n >= 1; --n)
        text.replace(
            QRegularExpression(QString("^h%1\\.[ \\t]*(.+)$").arg(n),
                               QRegularExpression::MultilineOption),
            QString("<h%1>\\1</h%1>").arg(n));

    // 8. Wiki links  [[tag|display]] then [[tag|]] (empty alias) then [[tag]]
    // Use opaque URI "wiki:TAG" - tag lands in url.path(), not url.host(),
    // so underscores and parens in tag names are handled correctly.
    text.replace(QRegularExpression(R"(\[\[([^\|\]]+)\|([^\]]+)\]\])"),
                 "<a href='wiki:\\1'>\\2</a>");
    text.replace(QRegularExpression(R"(\[\[([^\|\]]+)\|?\]\])"),
                 "<a href='wiki:\\1'>\\1</a>");

    // 8.5. Convert !post bullet groups into inline image galleries.
    // Runs after step 8 so wiki-link captions are already converted to <a> tags.
    {
        static const QRegularExpression postBulletRe(
            "^\\*+[ \\t]+!post #(\\d+)(?::\\s*(.*))?$");

        QStringList lines = text.split('\n');
        QStringList out;
        QList<QPair<int,QString>> gallery;

        auto flushGallery = [&]() {
            if (gallery.isEmpty()) return;
            const int maxPerRow = qMax(1, 800 / (ThumbW + 10));
            out << "<table class='gallery' cellspacing='0' cellpadding='0'>";
            for (int i = 0; i < gallery.size(); ++i) {
                if (i % maxPerRow == 0) {
                    if (i > 0) out << "</tr>";
                    out << "<tr>";
                }
                const int     id  = gallery[i].first;
                const QString cap = gallery[i].second.isEmpty()
                                      ? QString("post #%1").arg(id)
                                      : gallery[i].second;
                out << QString(
                    "<td class='thumb' align='center' style='padding:0 10px 4px 0;vertical-align:middle;width:%2px;'>"
                    "<a href='post:%1'><img src='post:%1'></a>"
                    "<br><small>%3</small></td>")
                    .arg(id).arg(ThumbW).arg(cap);
            }
            out << "</tr></table>";
            gallery.clear();
        };

        for (const QString& line : lines) {
            const QRegularExpressionMatch m = postBulletRe.match(line);
            if (m.hasMatch()) {
                gallery.append(qMakePair(m.captured(1).toInt(), m.captured(2).trimmed()));
            } else {
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

    // 10. Lists (line-by-line)
    {
        static const QRegularExpression ulRe("^\\*+[ \\t]+(.+)$");
        static const QRegularExpression olRe("^#+[ \\t]+(.+)$");
        QStringList lines = text.split('\n');
        QStringList out;
        bool inUl = false, inOl = false;
        for (const QString& line : lines) {
            const auto ulM = ulRe.match(line);
            const auto olM = olRe.match(line);
            if (ulM.hasMatch()) {
                if (inOl) { out << "</ol>"; inOl = false; }
                if (!inUl) { out << "<ul>"; inUl = true; }
                out << "<li>" + ulM.captured(1) + "</li>";
            } else if (olM.hasMatch()) {
                if (inUl) { out << "</ul>"; inUl = false; }
                if (!inOl) { out << "<ol>"; inOl = true; }
                out << "<li>" + olM.captured(1) + "</li>";
            } else {
                if (inUl) { out << "</ul>"; inUl = false; }
                if (inOl) { out << "</ol>"; inOl = false; }
                out << line;
            }
        }
        if (inUl) out << "</ul>";
        if (inOl) out << "</ol>";
        text = out.join('\n');
    }

    // 11. Restore external link tokens (display text is pre-processed HTML from step 3)
    for (auto it = linkTokens.cbegin(); it != linkTokens.cend(); ++it) {
        text.replace(it.key(),
            QString("<a href='%1'>%2</a>")
                .arg(it.value().second, it.value().first));
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
            } else {
                para.replace('\n', "<br/>");
                result << "<p>" + para + "</p>";
            }
        }
        text = result.join('\n');
    }

    // Font from the application's loaded custom font (ID 0 in QFontDatabase)
    const QStringList fontFamilies = QFontDatabase::applicationFontFamilies(0);
    const QString fontDecl = fontFamilies.isEmpty()
        ? QString()
        : QString("font-family:'%1',sans-serif;").arg(fontFamilies.first());

    const QString css =
        "<style>"
        "body{background:transparent;color:#c0c0c0;font-size:15px;margin:0;padding:0;" + fontDecl + "}"
        "h1,h2,h3,h4,h5,h6{color:#888;border-bottom:1px solid #222;padding-bottom:3px;margin-top:14px;}"
        "a{color:#5599cc;text-decoration:none;}"
        "blockquote{border-left:2px solid #333;margin:4px 0 4px 8px;padding-left:10px;color:#888;}"
        "code{background:#1a1a1a;border-radius:3px;padding:1px 4px;font-family:monospace;font-size:12px;}"
        "ul,ol{padding-left:20px;margin:4px 0;}"
        "li{margin:2px 0;}"
        "table.gallery{margin:6px 0;}"
        "td.thumb{color:#888;font-size:11px;}"
        ".ws{margin:6px 0;}"
        ".wsh{color:#666;font-weight:bold;font-size:11px;margin:0 0 3px 0;}"
        ".wtn{color:#666;font-size:11px;}"
        ".wsp{color:#555;}"
        "</style>";

    return "<html><head>" + css + "</head><body>"
           "<table width='100%' cellspacing='0' cellpadding='0'>"
           "<tr><td></td>"
           "<td width='800' style='padding:16px 8px;'>" + text + "</td>"
           "<td></td></tr>"
           "</table></body></html>";
}

} // namespace gui
