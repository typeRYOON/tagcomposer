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

// The dataset tools behind one tab bar. Each tool is independent; the handoffs
// between them are wired here.
class DatasetHelpersPage : public QWidget {
    Q_OBJECT

public:
    DatasetHelpersPage(Settings& settings, const TagFacets& facets,
                       TaggerLibrary& taggers, const QString& clusterFiltersPath,
                       const QString& collectionsRoot, QWidget* parent = nullptr);

    void setDanbooruIndex(const DanbooruIndex* index);

    // For the shell to wire the cluster page's navigation.
    TagClusterPage* clusterPage() const;

    void setQuickFacets(const QString& character, const QString& copyright,
                        const QString& triggerWord, const QString& style);
    void refreshFacets();

    // The active tab's label.
    QString currentTabLabel() const;

    // Called on close; stops the collector's watcher.
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

    // Outside the layout so it can animate; lowered beneath the buttons.
    QWidget* m_indicator = nullptr;
    QPointer<QPropertyAnimation> m_indicatorAnim;

    // Placed on first show, once the buttons have geometry.
    bool m_indicatorPlaced = false;
};

} // namespace tc
