#pragma once
#include <gui/widgets/tagsearchbar.h>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QHash>
#include <functional>

class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QTimer;

namespace gui {

class TagWikiPage : public QWidget {
    Q_OBJECT
public:
    explicit TagWikiPage(QWidget* parent = nullptr);
    void lookupTag(const QString& tag);
    void setDanbooruIndex(core::DanbooruIndex* index);

signals:
    void wikiLinkClicked(const QString& tag);

private slots:
    void onAnchorClicked(const QUrl& url);
    void goBack();
    void goForward();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void loadFromHistory();
    void fetchWikiPage(const QString& tag);
    void fetchPostData(int postId);
    void fetchAssetData(int assetId);
    void displayContent(const QString& title, const QStringList& otherNames, const QString& body);
    void showLoading();
    void showNotFound(const QString& tag);
    QString dtextToHtml(const QString& dtext, QList<int>& outPostIds, QList<int>& outAssetIds);

    // Animates the resource at `resourceUrl` from transparent to `finalPix`.
    void startThumbFade(const QString& resourceUrl, const QPixmap& finalPix);
    void onThumbFadeTick();

    // Paced dispatch of thumb metadata fetches; one job per tick keeps us
    // under Danbooru's per-IP rate limit on image-heavy pages.
    enum class ThumbKind { Post, Asset };
    void enqueueThumbFetch(ThumbKind kind, int id);
    void processThumbFetchQueue();

    void smoothScrollTo(int target);
    void smoothScrollToAnchor(const QString& anchor);

    // Fade out, then on the dark frame either emit wikiLinkClicked (Lookup)
    // or call loadFromHistory (History). The fade-in fires from displayContent
    // / showNotFound once the new content is ready.
    void startFadeOutThenLookup(const QString& tag);
    void startFadeOutThenHistory();
    void finishPendingFadeIn();
    void cancelPendingFade();

    // Fetch imageUrl, scale-and-pad to ThumbW x ThumbH, store via `store`,
    // then start the thumb-fade animation.
    void downloadThumbAndFade(const QString& imageUrl, const QString& resourceUrl,
                              std::function<void(const QPixmap&)> store);

    QNetworkAccessManager* m_nam;
    QString m_currentTag;
    QHash<QString, QByteArray> m_wikiCache;
    QHash<int, QPixmap> m_postThumbs;
    QHash<int, QPixmap> m_assetThumbs;
    bool m_fontApplied = false;
    bool m_screenChangedConnected = false;

    // Coalesced thumb-fade state: a single shared timer drives every in-flight
    // fade so a 100-thumb gallery only triggers one document re-layout per tick.
    struct PendingThumbFade {
        QString resourceUrl;
        QPixmap finalPix;
        int step;
    };
    QList<PendingThumbFade> m_pendingFades;
    QTimer* m_thumbFadeTimer = nullptr;

    struct QueuedThumbFetch {
        ThumbKind kind;
        int id;
    };
    QList<QueuedThumbFetch> m_thumbFetchQueue;
    QTimer* m_thumbFetchTimer = nullptr;

    QGraphicsOpacityEffect* m_fadeEffect = nullptr;
    QPropertyAnimation* m_fadeAnim = nullptr;
    QPropertyAnimation* m_scrollAnim = nullptr;
    QString m_pendingTag;
    bool m_pendingFadeIn = false;
    bool m_pendingHistoryNav = false;

    QList<QString> m_history;
    int m_historyPos = -1;
    bool m_navigating = false;

    TagSearchBar* m_searchBar;
    QPushButton* m_backBtn = nullptr;
    QPushButton* m_forwardBtn = nullptr;
    QPushButton* m_openExternalBtn = nullptr;
    QLabel* m_titleLabel;
    QLabel* m_aliasLabel;
    QTextBrowser* m_browser;
    QStackedWidget* m_mainStack;

    void updateNavButtons();
};

} // namespace gui
