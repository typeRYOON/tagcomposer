#pragma once
#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;

namespace tc {

// Danbooru preview for one tag: the wiki page's first "!post #N", else the top
// /posts.json hit. Only the newest fetch() reports.
class TagPreviewFetcher : public QObject {
    Q_OBJECT

public:
    explicit TagPreviewFetcher(QObject* parent = nullptr);

    // A cached tag re-emits synchronously, before this returns.
    void fetch(const QString& tag);

    // Abandons the in-flight fetch.
    void cancel();

signals:
    void loading(const QString& tag);

    // An empty body means no wiki page (not an error).
    void wikiBodyReady(const QString& tag, const QString& body);
    void imageReady(const QString& tag, const QPixmap& image, int postId);

    void failed(const QString& tag, const QString& reason);

private:
    void fetchPostById(const QString& tag, int postId);
    void fetchFirstPostByTag(const QString& tag);
    void fetchImage(const QString& tag, const QString& imageUrl);
    void cacheImage(const QString& tag, const QPixmap& image, int postId);

    QNetworkAccessManager* m_network = nullptr;
    quint64 m_generation = 0;

    // Only the image cache is capped.
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
