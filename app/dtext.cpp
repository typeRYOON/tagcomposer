#include <app/dtext.h>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>

namespace tc {
namespace {

// Inline tags only, for link titles.
QString applyInlineMarkup(const QString& raw)
{
    QString s = raw.toHtmlEscaped();
    s.replace(QRegularExpression(R"(\[b\](.*?)\[/b\])"), "<b>\\1</b>");
    s.replace(QRegularExpression(R"(\[i\](.*?)\[/i\])"), "<i>\\1</i>");
    s.replace(QRegularExpression(R"(\[u\](.*?)\[/u\])"), "<u>\\1</u>");
    s.replace(QRegularExpression(R"(\[s\](.*?)\[/s\])"), "<s>\\1</s>");
    return s;
}

} // namespace

QString dtextToHtml(const QString& dtext, QList<int>* outPostIds, QList<int>* outAssetIds,
                    int thumbWidth, int thumbHeight)
{
    const bool media = outPostIds != nullptr || outAssetIds != nullptr;

    QString text = dtext;

    // 1. The API returns CRLF.
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');

    // 2. Collect !post / !asset bullet ids, capped.
    if (media) {
        static const QRegularExpression bulletExtractRe(R"(^\*+[ \t]+!(post|asset) #(\d+))",
                                                        QRegularExpression::MultilineOption);
        constexpr int kMaxPerKind = 200;

        auto it = bulletExtractRe.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const int id = m.captured(2).toInt();

            if (m.captured(1) == QLatin1String("post")) {
                if (outPostIds && !outPostIds->contains(id) && outPostIds->size() < kMaxPerKind)
                    *outPostIds << id;
            } else {
                if (outAssetIds && !outAssetIds->contains(id)
                    && outAssetIds->size() < kMaxPerKind)
                    *outAssetIds << id;
            }
        }
    }

    // 3. Drop {{...}} tag-search embeds.
    text.remove(QRegularExpression(R"(\{\{[^}]*\}\})"));

    // 4. Swap external links for tokens before escaping: title, URL and an
    //    optional parenthetical display text.
    QHash<QString, QPair<QString, QString>> linkTokens; // token -> {displayHtml, url}
    {
        static const QRegularExpression linkRe(
            R"~("([^"]+)":\[?((?:https?://|/|#)[^\]\s"]+)\]?(?:\s*\(([^)]*)\))?)~");

        int n = 0;
        qsizetype offset = 0;
        while (true) {
            const QRegularExpressionMatch m = linkRe.match(text, offset);
            if (!m.hasMatch()) break;

            const QString rawTitle = m.captured(1);
            const QString rawUrl = m.captured(2);
            const QString rawParen = m.captured(3);

            const QString url = rawUrl.startsWith('/')
                ? QStringLiteral("https://danbooru.donmai.us") + rawUrl
                : rawUrl;
            const QString display =
                rawParen.isEmpty() ? applyInlineMarkup(rawTitle) : rawParen.toHtmlEscaped();

            const QString token = QStringLiteral("__LNKTOK%1__").arg(n++);
            linkTokens[token] = {display, url};
            text.replace(m.capturedStart(), m.capturedLength(), token);
            offset = m.capturedStart() + token.size();
        }
    }

    text = text.toHtmlEscaped();

    // 5. Block elements
    static const QRegularExpression sectionRe(
        R"(\[section(?:,expanded)?=([^\]]*)\](.*?)\[/section\])",
        QRegularExpression::DotMatchesEverythingOption
            | QRegularExpression::CaseInsensitiveOption);
    text.replace(sectionRe, "<div class='ws'><p class='wsh'>\\1</p>\\2</div>");

    // QTextBrowser cannot collapse [expand], so it renders as a section.
    static const QRegularExpression expandRe(
        R"(\[expand(?:=([^\]]*))?\](.*?)\[/expand\])",
        QRegularExpression::DotMatchesEverythingOption
            | QRegularExpression::CaseInsensitiveOption);
    text.replace(expandRe, "<div class='ws'><p class='wsh'>\\1</p>\\2</div>");

    text.replace(QRegularExpression(R"(\[quote\](.*?)\[/quote\])",
                                    QRegularExpression::DotMatchesEverythingOption
                                        | QRegularExpression::CaseInsensitiveOption),
                 "<blockquote>\\1</blockquote>");

    text.replace(QRegularExpression(R"(\[spoiler(?:s)?\](.*?)\[/spoiler(?:s)?\])",
                                    QRegularExpression::DotMatchesEverythingOption
                                        | QRegularExpression::CaseInsensitiveOption),
                 "<span class='wsp'>\\1</span>");

    text.replace(QRegularExpression(R"(\[tn\](.*?)\[/tn\])",
                                    QRegularExpression::DotMatchesEverythingOption
                                        | QRegularExpression::CaseInsensitiveOption),
                 "<small class='wtn'>\\1</small>");

    text.replace(QRegularExpression(R"(\[code\](.*?)\[/code\])",
                                    QRegularExpression::DotMatchesEverythingOption
                                        | QRegularExpression::CaseInsensitiveOption),
                 "<code>\\1</code>");

    // 6. Inline formatting
    text.replace(QRegularExpression(R"(\[b\](.*?)\[/b\])",
                                    QRegularExpression::DotMatchesEverythingOption),
                 "<b>\\1</b>");
    text.replace(QRegularExpression(R"(\[i\](.*?)\[/i\])",
                                    QRegularExpression::DotMatchesEverythingOption),
                 "<i>\\1</i>");
    text.replace(QRegularExpression(R"(\[u\](.*?)\[/u\])",
                                    QRegularExpression::DotMatchesEverythingOption),
                 "<u>\\1</u>");
    text.replace(QRegularExpression(R"(\[s\](.*?)\[/s\])",
                                    QRegularExpression::DotMatchesEverythingOption),
                 "<s>\\1</s>");
    text.remove(
        QRegularExpression(R"(\[color=[^\]]*\])", QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression(R"(\[/color\])", QRegularExpression::CaseInsensitiveOption));

    // 7. Headers get a dtext-<slug> anchor, matching Danbooru's own TOC links.
    auto slugify = [](const QString& title) {
        QString s = title;
        s.remove(QRegularExpression("<[^>]*>"));
        s.remove(QRegularExpression("\\[/?[a-zA-Z]+(?:=[^\\]]*)?\\]"));
        s = s.toLower();
        s.replace(QRegularExpression("[^a-z0-9]+"), "-");
        while (s.startsWith('-')) s = s.sliced(1);
        while (s.endsWith('-')) s.chop(1);
        return QStringLiteral("dtext-") + s;
    };

    for (int n = 6; n >= 1; --n) {
        text.replace(QRegularExpression(QStringLiteral("^h%1#([\\w-]+)\\.[ \\t]*(.+)$").arg(n),
                                        QRegularExpression::MultilineOption),
                     QStringLiteral("<h%1><a name='dtext-\\1'></a>\\2</h%1>").arg(n));

        // The bare form needs a slug per match.
        const QRegularExpression bareRe(QStringLiteral("^h%1\\.[ \\t]*(.+)$").arg(n),
                                        QRegularExpression::MultilineOption);
        QString rebuilt;
        qsizetype last = 0;

        auto it = bareRe.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            rebuilt += text.sliced(last, m.capturedStart() - last);

            const QString title = m.captured(1);
            rebuilt += QStringLiteral("<h%1><a name='%2'></a>%3</h%1>")
                           .arg(QString::number(n), slugify(title), title);
            last = m.capturedEnd();
        }
        if (last > 0) {
            rebuilt += text.sliced(last);
            text = rebuilt;
        }
    }

    // 8. Wiki links as opaque "wiki:TAG" URIs so url.path() keeps the tag intact.
    text.replace(QRegularExpression(R"(\[\[([^\|\]]+)\|([^\]]+)\]\])"),
                 "<a href='wiki:\\1'>\\2</a>");
    text.replace(QRegularExpression(R"(\[\[([^\|\]]+)\|?\]\])"), "<a href='wiki:\\1'>\\1</a>");

    // 9. Runs of !post / !asset bullets become one gallery table.
    if (media) {
        static const QRegularExpression bulletRe(
            "^\\*+[ \\t]+!(post|asset) #(\\d+)(?::\\s*(.*))?$");

        struct GalleryItem {
            QString kind;
            int id = 0;
            QString caption;
        };

        QStringList out;
        QList<GalleryItem> gallery;

        auto flushGallery = [&]() {
            if (gallery.isEmpty()) return;

            const int perRow = qMax(1, 800 / (thumbWidth + 10));
            out << "<table class='gallery' cellspacing='0' cellpadding='0'>";

            for (int i = 0; i < int(gallery.size()); ++i) {
                if (i % perRow == 0) {
                    if (i > 0) out << "</tr>";
                    out << "<tr>";
                }

                const GalleryItem& item = gallery[i];
                const QString caption = item.caption.isEmpty()
                    ? QStringLiteral("%1 #%2").arg(item.kind).arg(item.id)
                    : item.caption;

                // Explicit size keeps cells in logical pixels across DPR changes.
                out << QStringLiteral("<td class='thumb' align='center' style='padding:0 10px "
                                      "4px 0;vertical-align:middle;width:%3px;'>"
                                      "<a href='%1:%2'><img src='%1:%2' width='%3' "
                                      "height='%5'></a><br><small>%4</small></td>")
                           .arg(item.kind)
                           .arg(item.id)
                           .arg(thumbWidth)
                           .arg(caption)
                           .arg(thumbHeight);
            }

            out << "</tr></table>";
            gallery.clear();
        };

        for (const QString& line : text.split('\n')) {
            const QRegularExpressionMatch m = bulletRe.match(line);
            if (m.hasMatch()) {
                gallery.append({m.captured(1), m.captured(2).toInt(), m.captured(3).trimmed()});
                continue;
            }
            flushGallery();
            out << line;
        }
        flushGallery();
        text = out.join('\n');
    } else {
        // The leading `*` stays so the list pass below still bullets it.
        static const QRegularExpression plainMediaRe(
            R"(^(\*+[ \t]+)!(post|asset) #(\d+)(?::\s*(.*))?$)",
            QRegularExpression::MultilineOption);
        text.replace(plainMediaRe, R"(\1<a href='\2:\3'>\2 #\3</a> \4)");
    }

    // 10. Standalone "post #N" becomes a link.
    text.replace(QRegularExpression(R"(\bpost #(\d+))"),
                 "<a href='https://danbooru.donmai.us/posts/\\1'>post #\\1</a>");

    // 11. Lists: the leading * or # count is the depth; switching kinds closes the other.
    {
        static const QRegularExpression unorderedRe("^(\\*+)[ \\t]+(.+)$");
        static const QRegularExpression orderedRe("^(#+)[ \\t]+(.+)$");

        QStringList out;
        int unorderedDepth = 0;
        int orderedDepth = 0;

        auto closeAll = [&]() {
            while (unorderedDepth > 0) {
                out << "</ul>";
                --unorderedDepth;
            }
            while (orderedDepth > 0) {
                out << "</ol>";
                --orderedDepth;
            }
        };

        for (const QString& line : text.split('\n')) {
            const QRegularExpressionMatch unordered = unorderedRe.match(line);
            const QRegularExpressionMatch ordered = orderedRe.match(line);

            if (unordered.hasMatch()) {
                while (orderedDepth > 0) {
                    out << "</ol>";
                    --orderedDepth;
                }
                const int target = int(unordered.captured(1).size());
                while (unorderedDepth < target) {
                    out << "<ul>";
                    ++unorderedDepth;
                }
                while (unorderedDepth > target) {
                    out << "</ul>";
                    --unorderedDepth;
                }
                out << "<li>" + unordered.captured(2) + "</li>";
            } else if (ordered.hasMatch()) {
                while (unorderedDepth > 0) {
                    out << "</ul>";
                    --unorderedDepth;
                }
                const int target = int(ordered.captured(1).size());
                while (orderedDepth < target) {
                    out << "<ol>";
                    ++orderedDepth;
                }
                while (orderedDepth > target) {
                    out << "</ol>";
                    --orderedDepth;
                }
                out << "<li>" + ordered.captured(2) + "</li>";
            } else {
                closeAll();
                out << line;
            }
        }
        closeAll();
        text = out.join('\n');
    }

    // 12. Restore the external links.
    for (auto it = linkTokens.cbegin(); it != linkTokens.cend(); ++it)
        text.replace(it.key(),
                     QStringLiteral("<a href='%1'>%2</a>").arg(it.value().second, it.value().first));

    // 13. Paragraphs, leaving block elements unwrapped.
    {
        static const QRegularExpression blockRe(
            "^\\s*</?(?:h[1-6]|ul|ol|li|table|tr|td|blockquote|div)[\\s>/]",
            QRegularExpression::CaseInsensitiveOption);

        QStringList result;
        for (QString paragraph : text.split(QRegularExpression("\\n{2,}"))) {
            paragraph = paragraph.trimmed();
            if (paragraph.isEmpty()) continue;

            if (blockRe.match(paragraph).hasMatch()) {
                paragraph.replace('\n', "");
                result << paragraph;
            } else {
                paragraph.replace('\n', "<br/>");
                result << "<p>" + paragraph + "</p>";
            }
        }
        text = result.join('\n');
    }

    return text;
}

} // namespace tc
