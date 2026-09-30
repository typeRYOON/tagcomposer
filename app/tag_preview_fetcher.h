#pragma once
#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;

namespace tc {

// Danbooru preview lookup for one tag, shared by the facet editor's rail and
// the composer's hover popup.
//
// The chain matches what the wiki page shows, so previews agree across the
// app: the tag's wiki page first, taking its first "!post #N" when it has
// one, otherwise the top hit of a /posts.json search.
//
// Only the newest fetch() reports back - an earlier chain still in flight is
// dropped - so a caller never has to guard against a stale reply landing last.
class TagPreviewFetcher : public QObject {
    Q_OBJECT

public:
    explicit TagPreviewFetcher(QObject* parent = nullptr);

    // A cached tag re-emits synchronously, before this returns.
    void fetch(const QString& tag);

    // Abandons the in-flight chain; nothing further is emitted for it.
    void cancel();

signals:
    void loading(const QString& tag);

    // Once the wiki page resolves. An empty body is not an error - the tag
    // simply has no wiki page, and the image search still runs.
    void wikiBodyReady(const QString& tag, const QString& body);
    void imageReady(const QString& tag, const QPixmap& image, int postId);

    // Terminal, carrying the short text the rails show.
    void failed(const QString& tag, const QString& reason);

private:
    void fetchPostById(const QString& tag, int postId);
    void fetchFirstPostByTag(const QString& tag);
    void fetchImage(const QString& tag, const QString& imageUrl);
    void cacheImage(const QString& tag, const QPixmap& image, int postId);

    QNetworkAccessManager* m_network = nullptr;
    quint64 m_generation = 0;

    // Images are capped because the hover popup can walk a lot of tags in one
    // session. Wiki bodies and post ids are small enough to keep.
    QHash<QString, QPixmap> m_imageCache;
    QStringList m_imageOrder; // oldest first, for the cap
    QHash<QString, int> m_postIds;
    QHash<QString, QString> m_wikiBodies; // an empty value means "no wiki page"
};

// Scale to fit, then clip to a rounded rect.
QPixmap roundedPreview(const QPixmap& source, int maxWidth, int maxHeight, qreal radius = 6.0);

// The wiki page's palette at panel type sizes.
QString wikiPanelCss();

} // namespace tc
