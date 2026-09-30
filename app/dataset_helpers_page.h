#pragma once
#include <QPointer>
#include <QString>
#include <QWidget>

class QButtonGroup;
class QHBoxLayout;
class QPropertyAnimation;
class QStackedWidget;

namespace tc {

class AutoTagPage;
class BatchEditPage;
class CollectorPage;
class DanbooruIndex;
struct Settings;
class TagClusterPage;
class TagEditorPage;
class TagFacets;
class TaggerLibrary;

// Holds the dataset tools behind one tab bar. Each tool is its own page and
// knows nothing about the others; the handoffs between them are wired here,
// so a tool stays usable on its own.
class DatasetHelpersPage : public QWidget {
    Q_OBJECT

public:
    DatasetHelpersPage(Settings& settings, const TagFacets& facets,
                       TaggerLibrary& taggers, const QString& clusterFiltersPath,
                       const QString& collectionsRoot, QWidget* parent = nullptr);

    // Arrives with the library, like every other page's index.
    void setDanbooruIndex(const DanbooruIndex* index);

    // The shell wires the cluster page's navigation signals itself, the
    // same way it does for every other page that emits them.
    TagClusterPage* clusterPage() const;

    void setQuickFacets(const QString& character, const QString& copyright,
                        const QString& triggerWord, const QString& style);
    void refreshFacets();

    // The active tab's label. The shell appends it to the window title.
    QString currentTabLabel() const;

    // The shell calls this on close: the collector moves files on a timer and
    // must not outlive the window.
    void stopBackgroundWork();

signals:
    void tabChanged(const QString& label);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void addTab(const QString& label, QWidget* page);
    void moveIndicatorTo(QWidget* button, bool animate);
    void switchTo(QWidget* page);

    TagClusterPage* m_clusterPage = nullptr;
    CollectorPage* m_collectorPage = nullptr;
    AutoTagPage* m_autoTagPage = nullptr;
    TagEditorPage* m_editorPage = nullptr;
    BatchEditPage* m_batchPage = nullptr;

    QWidget* m_tabBar = nullptr;
    QHBoxLayout* m_tabLayout = nullptr;
    QButtonGroup* m_tabGroup = nullptr;
    QStackedWidget* m_stack = nullptr;

    // Outside the layout so it can be animated on its own, and lowered so the
    // button text paints over it.
    QWidget* m_indicator = nullptr;
    QPointer<QPropertyAnimation> m_indicatorAnim;

    // The buttons have no geometry until the first show, so the first place
    // cannot animate and cannot happen in the constructor.
    bool m_indicatorPlaced = false;
};

} // namespace tc
