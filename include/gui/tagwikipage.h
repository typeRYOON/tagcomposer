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
    // Watches m_browser for resize events; QTextDocument's lazy re-flow can
    // leave text overlapping images when the window jumps to a wider monitor,
    // so we explicitly invalidate the layout when the viewport grows.
    bool eventFilter(QObject* obj, QEvent* event) override;
    // Connects the top-level window's screenChanged signal on first show.
    // Same-logical-size moves to a different-DPR screen don't fire Resize on
    // the browser, so the eventFilter alone wouldn't catch them.
    void showEvent(QShowEvent* event) override;

private:
    void loadFromHistory();
    void fetchWikiPage(const QString& tag);
    void fetchPostData(int postId);
    // Asset references (`* !asset #N` in DText) resolve via the media_assets
    // JSON endpoint instead of /posts/. Same downstream flow once we have a
    // thumbnail URL: download, scale, fade in.
    void fetchAssetData(int assetId);
    void displayContent(const QString& title, const QStringList& otherNames, const QString& body);
    void showLoading();
    void showNotFound(const QString& tag);
    QString dtextToHtml(const QString& dtext, QList<int>& outPostIds, QList<int>& outAssetIds);

    // Replaces the resource at `resourceUrl` progressively from transparent
    // to `finalPix` over a short animation. Keeps the broken-image icon
    // from flashing before a thumbnail loads, and gives a soft entrance
    // once it does. Resource URL is something like "post:42" or "asset:9001".
    void startThumbFade(const QString& resourceUrl, const QPixmap& finalPix);

    // Animates the browser's vertical scrollbar to `target`, clamped to the
    // scrollbar's current min/max. If a previous scroll animation is running,
    // its endValue becomes the base so wheel events stack instead of fighting.
    void smoothScrollTo(int target);

    // scrollToAnchor's animated counterpart. Reads the target position by
    // briefly snap-jumping to the anchor, snaps back, then animates. The
    // snap-and-restore happens within one call so no paint event lands in
    // between - the user only sees the smooth scroll.
    void smoothScrollToAnchor(const QString& anchor);

    // Stages a wiki-page transition: queue `tag`, animate stack opacity to
    // 0, and let the fade-finish handler emit wikiLinkClicked once the
    // fade-out lands. The matching fade-in fires from displayContent /
    // showNotFound when the new content is ready. Used by the search bar
    // commit path and the in-document [[wiki link]] click path.
    void startFadeOutThenLookup(const QString& tag);

    // History-navigation counterpart to startFadeOutThenLookup. Caller has
    // already advanced m_historyPos; the fade-finished handler picks up
    // m_pendingHistoryNav and calls loadFromHistory at the dark frame.
    void startFadeOutThenHistory();

    // If a fade-in was queued by the previous fade-out, animate opacity
    // back to 1.0. No-op when m_pendingFadeIn is false (initial show,
    // history navigation, programmatic lookupTag from outside).
    void finishPendingFadeIn();

    // Stops any running fade animation, drops m_pendingTag / m_pendingFadeIn,
    // and snaps opacity back to 1.0. Used by history navigation so an
    // in-flight wiki-link fade can't hijack the back/forward target.
    void cancelPendingFade();

    // Shared step 4-7 of the post / asset thumbnail flow: GET imageUrl,
    // scale and centre on a ThumbW x ThumbH canvas, hand the result to
    // `store` (writes the per-kind cache), then call startThumbFade. Lives
    // on the class so it can reach startThumbFade and m_nam directly.
    void downloadThumbAndFade(const QString& imageUrl, const QString& resourceUrl,
                              std::function<void(const QPixmap&)> store);

    QNetworkAccessManager* m_nam;
    QString m_currentTag;
    QHash<QString, QByteArray> m_wikiCache;
    QHash<int, QPixmap> m_postThumbs;
    QHash<int, QPixmap> m_assetThumbs;
    bool m_fontApplied = false;
    bool m_screenChangedConnected = false;

    // Crossfade between wiki pages when the user clicks an in-document
    // [[wiki link]]. Search-bar lookups and history navigation skip the
    // animation - only set m_pendingTag from the wiki link click path.
    QGraphicsOpacityEffect* m_fadeEffect = nullptr;
    QPropertyAnimation* m_fadeAnim = nullptr;
    QPropertyAnimation* m_scrollAnim = nullptr;
    QString m_pendingTag; // tag to emit when fade-out finishes
    bool m_pendingFadeIn = false;
    // True when goBack / goForward kicked off a fade-out and we should
    // resolve by loading from history rather than emitting wikiLinkClicked.
    bool m_pendingHistoryNav = false;

    // Navigation history
    QList<QString> m_history;
    int m_historyPos = -1;
    bool m_navigating = false;

    // UI
    TagSearchBar* m_searchBar;
    QPushButton* m_backBtn = nullptr;
    QPushButton* m_forwardBtn = nullptr;
    QPushButton* m_openExternalBtn = nullptr;
    QLabel* m_titleLabel;
    QLabel* m_aliasLabel;
    QTextBrowser* m_browser;
    QStackedWidget* m_mainStack;

    // Refresh enabled state of the three nav buttons (back/forward by
    // history bounds, open-external by whether a tag is loaded).
    void updateNavButtons();
};

} // namespace gui
