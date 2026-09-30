#pragma once
#include <QRect>
#include <QString>
#include <QWidget>

class QAction;
class QLabel;
class QMenu;
class QTextBrowser;
class QTimer;

namespace tc {

class TagPreviewFetcher;

// Danbooru image and wiki text for a tag, shown beside an open menu while its
// Wiki item is highlighted. A tooltip-class window, so the menu keeps its grab
// (and this can't be clicked or scrolled).
class TagPreviewPopup : public QWidget {
    Q_OBJECT

public:
    explicit TagPreviewPopup(QWidget* parent = nullptr);

    // anchor: the menu's global geometry; the popup sits beside it.
    void scheduleShow(const QString& tag, const QRect& anchor);

    // Cancels a pending show and hides an open one.
    void dismiss();

private:
    void showNow();
    void applyGeometry();
    void position();

    TagPreviewFetcher* m_fetcher = nullptr;
    QTimer* m_delay = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_image = nullptr;
    QLabel* m_status = nullptr;
    QTextBrowser* m_wiki = nullptr;
    QString m_tag;
    QRect m_anchor;
};

// Shows the peek while wikiAction is highlighted (mouse or keyboard). Call
// popup->dismiss() after exec().
void installWikiPeek(QMenu& menu, QAction* wikiAction, const QString& tag,
                     TagPreviewPopup* popup);

} // namespace tc
