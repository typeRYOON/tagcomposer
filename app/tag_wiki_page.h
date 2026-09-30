#pragma once
#include <QHash>
#include <QList>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>

class QGraphicsOpacityEffect;
class QLabel;
class QNetworkAccessManager;
class QPropertyAnimation;
class QPushButton;
class QStackedWidget;
class QTextBrowser;
class QTimer;
class QUrl;

namespace tc {

class DanbooruIndex;
class TagSearchBar;

// The Danbooru wiki for one tag, rendered in-app: the article body, its
// `!post #N` thumbnail galleries, back/forward history, and an autocomplete
// bar to jump to another tag.
//
// Every page transition fades out, swaps, and fades back in, so a lookup
// never flashes the previous article's layout.
class TagWikiPage : public QWidget {
    Q_OBJECT

public:
    explicit TagWikiPage(QWidget* parent = nullptr);

    void setIndex(const DanbooruIndex* index);
    void lookupTag(const QString& tag);

signals:
    // A [[wiki link]] inside the body. The shell routes it back to
    // lookupTag, so history and the fade stay in one place.
    void wikiLinkClicked(const QString& tag);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onAnchorClicked(const QUrl& url);
    void goBack();
    void goForward();

private:
    void loadFromHistory();
    void fetchWikiPage(const QString& tag);
    void fetchPostData(int postId);
    void fetchAssetData(int assetId);
    void displayContent(const QString& title, const QStringList& otherNames,
                        const QString& body);
    void showLoading();
    void showNotFound(const QString& tag);
    QString buildDocument(const QString& dtext, QList<int>& outPostIds,
                          QList<int>& outAssetIds) const;

    // Fetches the image, pads it to the thumbnail box, stores it, then starts
    // its fade.
    void downloadThumbAndFade(const QString& imageUrl, const QString& resourceUrl,
                              std::function<void(const QPixmap&)> store);

    void startThumbFade(const QString& resourceUrl, const QPixmap& finalPixmap);
    void onThumbFadeTick();

    // Metadata fetches are paced: one per tick keeps an image-heavy page
    // under Danbooru's per-IP rate limit.
    enum class ThumbKind { Post, Asset };
    void enqueueThumbFetch(ThumbKind kind, int id);
    void processThumbFetchQueue();

    void smoothScrollTo(int target);
    void smoothScrollToAnchor(const QString& anchor);

    void startFadeOutThenLookup(const QString& tag);
    void startFadeOutThenHistory();
    void finishPendingFadeIn();

    void updateNavButtons();

    QNetworkAccessManager* m_network = nullptr;
    QString m_currentTag;
    QHash<QString, QByteArray> m_wikiCache;
    QHash<int, QPixmap> m_postThumbs;
    QHash<int, QPixmap> m_assetThumbs;
    bool m_fontApplied = false;

    // One shared ticker drives every in-flight fade, so a hundred-thumbnail
    // gallery costs one document re-layout per tick rather than a hundred.
    struct PendingThumbFade {
        QString resourceUrl;
        QPixmap finalPixmap;
        int step = 0;
    };
    QList<PendingThumbFade> m_pendingFades;
    QTimer* m_thumbFadeTimer = nullptr;

    struct QueuedThumbFetch {
        ThumbKind kind = ThumbKind::Post;
        int id = 0;
    };
    QList<QueuedThumbFetch> m_thumbFetchQueue;
    QTimer* m_thumbFetchTimer = nullptr;

    QGraphicsOpacityEffect* m_fadeEffect = nullptr;
    QPropertyAnimation* m_fadeAnim = nullptr;
    QPropertyAnimation* m_scrollAnim = nullptr;
    QString m_pendingTag;
    bool m_pendingFadeIn = false;
    bool m_pendingHistoryNav = false;

    QStringList m_history;
    int m_historyPos = -1;
    bool m_navigating = false;

    TagSearchBar* m_searchBar = nullptr;
    QPushButton* m_backBtn = nullptr;
    QPushButton* m_forwardBtn = nullptr;
    QPushButton* m_openExternalBtn = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_aliasLabel = nullptr;
    QTextBrowser* m_browser = nullptr;
    QStackedWidget* m_mainStack = nullptr;
};

} // namespace tc
