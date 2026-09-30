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

// The Danbooru image and wiki text for one tag, shown beside an open menu
// while the pointer rests on its Wiki item. Same content as the facet
// editor's rail, in a tooltip-class window so the menu keeps its grab and
// stays open.
//
// Read-only by construction: a menu owns the mouse for as long as this is up,
// so nothing here can be clicked or scrolled. A long wiki body is clipped
// rather than scrolled, which is what a peek wants.
class TagPreviewPopup : public QWidget {
    Q_OBJECT

public:
    explicit TagPreviewPopup(QWidget* parent = nullptr);

    // Arms the hover delay. `anchor` is the menu's geometry in global
    // coordinates; the popup lands beside it and flips side at a screen edge.
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

// Ties the peek to `menu`'s highlighted item: up while `wikiAction` is the
// highlight, gone for anything else. QMenu::hovered covers keyboard travel
// too, so arrowing onto the item behaves like pointing at it. The caller
// dismisses the popup once exec() returns.
void installWikiPeek(QMenu& menu, QAction* wikiAction, const QString& tag,
                     TagPreviewPopup* popup);

} // namespace tc
