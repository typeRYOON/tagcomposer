#include <utils/dtext.h>
#include <QRegularExpression>
#include <QStringList>

namespace utils {

// DText is Danbooru wiki markup. Pure string work - the caller wraps the
// returned fragment in its own document + CSS.

static QString applyInlineMarkup(const QString& raw)
{
    QString s = raw.toHtmlEscaped();
    s.replace(QRegularExpression(R"(\[b\](.*?)\[/b\])"), "<b>\\1</b>");
    s.replace(QRegularExpression(R"(\[i\](.*?)\[/i\])"), "<i>\\1</i>");
    s.replace(QRegularExpression(R"(\[u\](.*?)\[/u\])"), "<u>\\1</u>");
    s.replace(QRegularExpression(R"(\[s\](.*?)\[/s\])"), "<s>\\1</s>");
    return s;
}

QString dtextToHtml(const QString& dtext, QList<int>* outPostIds, QList<int>* outAssetIds,
                    int thumbW, int thumbH)
{
    // No collector means the caller can't resolve post:/asset: image
    // resources, so media bullets render as links, not thumbnails.
    const bool media = (outPostIds != nullptr || outAssetIds != nullptr);
    QString text = dtext;

    // 0. Normalize line endings (Danbooru API returns \r\n)
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');

    // 1. Extract !post / !asset bullet IDs. Capped as a safety bound; Qt's
    //    network manager throttles to ~6 concurrent connections per host so
    //    the queue drains in order rather than slamming Danbooru.
    if (media) {
        static const QRegularExpression mediaBulletExtractRe(R"(^\*+[ \t]+!(post|asset) #(\d+))",
                                                             QRegularExpression::MultilineOption);
        constexpr int kMaxPerKind = 200;
        auto it = mediaBulletExtractRe.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            const QString kind = m.captured(1);
            const int id = m.captured(2).toInt();
            if (kind == "post") {
                if (!outPostIds->contains(id) && outPostIds->size() < kMaxPerKind)
                    *outPostIds << id;
            }
            else { // "asset"
                if (!outAssetIds->contains(id) && outAssetIds->size() < kMaxPerKind)
                    *outAssetIds << id;
            }
        }
    }

    // 2. Strip {{...}} tag-search embeds
    text.remove(QRegularExpression(R"(\{\{[^}]*\}\})"));

    // 3. Pre-tokenize external links before HTML escaping. Captures:
    //      1: link title (may contain DText inline markup)
    //      2: URL - https?://, Danbooru-relative /, or same-page #anchor
    //      3: optional parenthetical override for display text (pool/search links)
    QHash<QString, QPair<QString, QString>> linkTokens; // token -> {displayHtml, resolvedUrl}
    {
        static const QRegularExpression extRe(
            R"~("([^"]+)":\[?((?:https?://|/|#)[^\]\s"]+)\]?(?:\s*\(([^)]*)\))?)~");

        int n = 0;
        int offset = 0;
        while (true) {
            const QRegularExpressionMatch m = extRe.match(text, offset);
            if (!m.hasMatch()) break;

            const QString rawTitle = m.captured(1);
            const QString rawUrl = m.captured(2);
            const QString rawParen = m.captured(3);

            const QString resolvedUrl =
                rawUrl.startsWith('/') ? "https://danbooru.donmai.us" + rawUrl : rawUrl;
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

    // QTextBrowser can't actually collapse [expand], so render it as [section].
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

    // 7. Headers. Both explicit (`h%n#name. Title`) and bare (`h%n. Title`)
    // forms produce a `dtext-<slug>` anchor: that's the prefix Danbooru's
    // own renderer bakes into headings, and TOCs target it.
    auto slugify = [](const QString& title) -> QString {
        QString s = title;
        s.remove(QRegularExpression("<[^>]*>"));
        s.remove(QRegularExpression("\\[/?[a-zA-Z]+(?:=[^\\]]*)?\\]"));
        s = s.toLower();
        s.replace(QRegularExpression("[^a-z0-9]+"), "-");
        while (s.startsWith('-')) s = s.mid(1);
        while (s.endsWith('-')) s.chop(1);
        return "dtext-" + s;
    };

    for (int n = 6; n >= 1; --n) {
        text.replace(QRegularExpression(QString("^h%1#([\\w-]+)\\.[ \\t]*(.+)$").arg(n),
                                        QRegularExpression::MultilineOption),
                     QString("<h%1><a name='dtext-\\1'></a>\\2</h%1>").arg(n));

        // Bare form: per-match slug requires manual iteration.
        const QRegularExpression bareRe(QString("^h%1\\.[ \\t]*(.+)$").arg(n),
                                        QRegularExpression::MultilineOption);
        QString rebuilt;
        int last = 0;
        auto it = bareRe.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            rebuilt += text.mid(last, m.capturedStart() - last);
            const QString title = m.captured(1);
            rebuilt += QString("<h%1><a name='%2'></a>%3</h%1>")
                           .arg(QString::number(n), slugify(title), title);
            last = m.capturedEnd();
        }
        if (last > 0) {
            rebuilt += text.mid(last);
            text = rebuilt;
        }
    }

    // 8. Wiki links: opaque URI "wiki:TAG" so url.path() (not host) gets the
    // tag, which preserves underscores and parens.
    text.replace(QRegularExpression(R"(\[\[([^\|\]]+)\|([^\]]+)\]\])"),
                 "<a href='wiki:\\1'>\\2</a>");
    text.replace(QRegularExpression(R"(\[\[([^\|\]]+)\|?\]\])"), "<a href='wiki:\\1'>\\1</a>");

    // 8.5. !post / !asset bullet runs become a single inline image gallery.
    // Mixed kinds flow into one table; the cells encode their own kind in href.
    if (media) {
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
            const int maxPerRow = qMax(1, 800 / (thumbW + 10));
            out << "<table class='gallery' cellspacing='0' cellpadding='0'>";
            for (int i = 0; i < gallery.size(); ++i) {
                if (i % maxPerRow == 0) {
                    if (i > 0) out << "</tr>";
                    out << "<tr>";
                }
                const GalleryItem& g = gallery[i];
                const QString cap =
                    g.caption.isEmpty() ? QString("%1 #%2").arg(g.kind).arg(g.id) : g.caption;
                // Explicit width/height on <img> locks the cell to logical
                // pixels: without them, DPR changes scale image cells but
                // not surrounding text.
                out << QString("<td class='thumb' align='center' style='padding:0 10px 4px "
                               "0;vertical-align:middle;width:%3px;'>"
                               "<a href='%1:%2'><img src='%1:%2' width='%3' height='%5'></a>"
                               "<br><small>%4</small></td>")
                           .arg(g.kind)
                           .arg(g.id)
                           .arg(thumbW)
                           .arg(cap)
                           .arg(thumbH);
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
    else {
        // Keep the leading `*` so the list pass below still bullets it.
        static const QRegularExpression plainMediaRe(
            R"(^(\*+[ \t]+)!(post|asset) #(\d+)(?::\s*(.*))?$)",
            QRegularExpression::MultilineOption);
        text.replace(plainMediaRe, R"(\1<a href='\2:\3'>\2 #\3</a> \4)");
    }

    // 9. Remaining standalone "post #N" references -> clickable links
    text.replace(QRegularExpression(R"(\bpost #(\d+))"),
                 "<a href='https://danbooru.donmai.us/posts/\\1'>post #\\1</a>");

    // 10. Lists: leading `*` / `#` count is nesting depth. Switching between
    // unordered and ordered closes the open type fully before opening the other.
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

    return text;
}

} // namespace utils
