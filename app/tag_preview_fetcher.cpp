#include <app/tag_preview_fetcher.h>
#include <core/entry.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

constexpr qsizetype kMaxCachedImages = 48;
constexpr auto kUserAgent = "TagComposer/1.0";

QNetworkRequest jsonRequest(const QUrl& url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
    request.setRawHeader("Accept", "application/json");
    return request;
}

// Skips formats with no still frame to show, and falls back to the thumbnail
// when the large render is missing.
QString pickPreviewUrl(const QJsonObject& post)
{
    static const QStringList nonImage = {u"mp4"_s, u"webm"_s, u"zip"_s, u"mov"_s, u"swf"_s};

    QString url;
    if (!nonImage.contains(post[u"file_ext"_s].toString().toLower()))
        url = post[u"large_file_url"_s].toString();
    if (url.isEmpty()) url = post[u"preview_file_url"_s].toString();
    return url;
}

} // namespace

QPixmap roundedPreview(const QPixmap& source, int maxWidth, int maxHeight, qreal radius)
{
    const QPixmap scaled =
        source.scaled(maxWidth, maxHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QPixmap rounded(scaled.size());
    rounded.fill(Qt::transparent);

    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    QPainterPath path;
    path.addRoundedRect(QRectF(rounded.rect()), radius, radius);
    painter.setClipPath(path);
    painter.drawPixmap(0, 0, scaled);
    return rounded;
}

QString wikiPanelCss()
{
    return u"<style>"
           "body{color:#9a9a9a;font-size:13px;}"
           "h1,h2,h3,h4,h5,h6{color:#888;font-size:13px;border-bottom:1px solid #222;"
           "padding-bottom:2px;margin-top:10px;}"
           "a{color:#66aa66;text-decoration:none;}"
           "blockquote{border-left:2px solid #333;margin:4px 0 4px 6px;padding-left:8px;"
           "color:#777;}"
           "code{background:#1a1a1a;border-radius:3px;padding:1px 3px;font-family:monospace;}"
           "ul,ol{padding-left:16px;margin:3px 0;}"
           "li{margin:2px 0;}"
           ".wsh{color:#666;font-weight:bold;font-size:11px;margin:0 0 3px 0;}"
           ".wtn{color:#666;font-size:11px;}"
           ".wsp{color:#555;}"
           "</style>"_s;
}

TagPreviewFetcher::TagPreviewFetcher(QObject* parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this))
{
}

void TagPreviewFetcher::cancel()
{
    ++m_generation;
}

void TagPreviewFetcher::fetch(const QString& tag)
{
    const quint64 generation = ++m_generation;

    if (m_imageCache.contains(tag)) {
        emit wikiBodyReady(tag, m_wikiBodies.value(tag));
        emit imageReady(tag, m_imageCache.value(tag), m_postIds.value(tag, -1));
        return;
    }

    emit loading(tag);

    // Step one: the wiki page, looking for its first "!post #N".
    const QByteArray encoded = QUrl::toPercentEncoding(serializeTag(tag));
    const QUrl url(u"https://danbooru.donmai.us/wiki_pages/%1.json"_s.arg(
        QString::fromLatin1(encoded)));

    QNetworkReply* reply = m_network->get(jsonRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, tag, reply, generation]() {
        reply->deleteLater();
        if (generation != m_generation) return;

        // A 404 only means the tag has no wiki page; the search still runs.
        const QJsonDocument doc = reply->error() == QNetworkReply::NoError
            ? QJsonDocument::fromJson(reply->readAll())
            : QJsonDocument();

        if (!doc.isObject()) {
            m_wikiBodies[tag] = QString();
            emit wikiBodyReady(tag, QString());
            fetchFirstPostByTag(tag);
            return;
        }

        const QString body = doc.object()[u"body"_s].toString();
        m_wikiBodies[tag] = body;
        emit wikiBodyReady(tag, body);

        static const QRegularExpression postRe(uR"(!post\s+#(\d+))"_s);
        const QRegularExpressionMatch match = postRe.match(body);
        if (match.hasMatch())
            fetchPostById(tag, match.captured(1).toInt());
        else
            fetchFirstPostByTag(tag);
    });
}

void TagPreviewFetcher::fetchPostById(const QString& tag, int postId)
{
    const QUrl url(u"https://danbooru.donmai.us/posts/%1.json"_s.arg(postId));
    const quint64 generation = m_generation;

    QNetworkReply* reply = m_network->get(jsonRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, tag, postId, reply, generation]() {
        reply->deleteLater();
        if (generation != m_generation) return;

        const QJsonDocument doc = reply->error() == QNetworkReply::NoError
            ? QJsonDocument::fromJson(reply->readAll())
            : QJsonDocument();

        if (!doc.isObject()) {
            fetchFirstPostByTag(tag);
            return;
        }

        const QString imageUrl = pickPreviewUrl(doc.object());
        if (imageUrl.isEmpty()) {
            fetchFirstPostByTag(tag);
            return;
        }

        m_postIds[tag] = postId;
        fetchImage(tag, imageUrl);
    });
}

void TagPreviewFetcher::fetchFirstPostByTag(const QString& tag)
{
    QUrl url(u"https://danbooru.donmai.us/posts.json"_s);
    QUrlQuery query;
    query.addQueryItem(u"tags"_s, serializeTag(tag));
    query.addQueryItem(u"limit"_s, u"1"_s);
    url.setQuery(query);

    const quint64 generation = m_generation;
    QNetworkReply* reply = m_network->get(jsonRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, tag, reply, generation]() {
        reply->deleteLater();
        if (generation != m_generation) return;

        if (reply->error() != QNetworkReply::NoError) {
            emit failed(tag, u"(no preview)"_s);
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isArray() || doc.array().isEmpty()) {
            emit failed(tag, u"(no posts)"_s);
            return;
        }

        const QJsonObject post = doc.array().first().toObject();
        const QString imageUrl = pickPreviewUrl(post);
        if (imageUrl.isEmpty()) {
            emit failed(tag, u"(no preview)"_s);
            return;
        }

        m_postIds[tag] = post[u"id"_s].toInt();
        fetchImage(tag, imageUrl);
    });
}

void TagPreviewFetcher::fetchImage(const QString& tag, const QString& imageUrl)
{
    QNetworkRequest request{QUrl(imageUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));

    const quint64 generation = m_generation;
    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, tag, reply, generation]() {
        reply->deleteLater();
        if (generation != m_generation) return;

        if (reply->error() != QNetworkReply::NoError) {
            emit failed(tag, u"(image fetch failed)"_s);
            return;
        }

        QPixmap image;
        if (!image.loadFromData(reply->readAll()) || image.isNull()) {
            emit failed(tag, u"(image decode failed)"_s);
            return;
        }

        const int postId = m_postIds.value(tag, -1);
        cacheImage(tag, image, postId);
        emit imageReady(tag, image, postId);
    });
}

void TagPreviewFetcher::cacheImage(const QString& tag, const QPixmap& image, int postId)
{
    m_postIds[tag] = postId;
    if (!m_imageCache.contains(tag)) m_imageOrder << tag;
    m_imageCache[tag] = image;

    while (m_imageOrder.size() > kMaxCachedImages)
        m_imageCache.remove(m_imageOrder.takeFirst());
}

} // namespace tc
