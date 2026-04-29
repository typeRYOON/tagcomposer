#pragma once
#include <gui/tagsearchbar.h>
#include <QWidget>
#include <QLabel>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QHash>

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

private:
    void loadFromHistory();
    void fetchWikiPage(const QString& tag);
    void fetchPostData(int postId);
    void displayContent(const QString& title,
                        const QStringList& otherNames,
                        const QString& body);
    void showLoading();
    void showNotFound(const QString& tag);
    QString dtextToHtml(const QString& dtext, QList<int>& outPostIds);

    QNetworkAccessManager*     m_nam;
    QString                    m_currentTag;
    QHash<QString, QByteArray> m_wikiCache;
    QHash<int, QPixmap>        m_thumbCache;
    bool                       m_fontApplied = false;

    // Navigation history
    QList<QString> m_history;
    int            m_historyPos = -1;
    bool           m_navigating = false;

    // UI
    TagSearchBar*   m_searchBar;
    QLabel*         m_titleLabel;
    QLabel*         m_aliasLabel;
    QTextBrowser*   m_browser;
    QStackedWidget* m_mainStack;
};

} // namespace gui
