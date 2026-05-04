#pragma once
#include <QWidget>
#include <QStackedWidget>
#include <QPushButton>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QPointer>

class QPropertyAnimation;

namespace core {
class FacetIndex;
class AutoTaggerLibrary;
} // namespace core
namespace utils {
struct AppSettings;
}

namespace gui {

class TagClusterPage;
class AutoTagPage;
class TagEditorPage;
class BatchEditPage;
class CollectorPage;

class DatasetHelpersPage : public QWidget {
    Q_OBJECT
public:
    DatasetHelpersPage(core::FacetIndex* facets, core::AutoTaggerLibrary* taggerLibrary,
                       utils::AppSettings* settings, QWidget* parent = nullptr);

    // AppMainWindow wires global signals (wiki / facet-editor / quick-add) to
    // each sub-page directly; expose the inner pages through accessors so the
    // top-level connection setup mirrors what it does for the other pages.
    TagClusterPage* tagClusterPage() const
    {
        return m_tagClusterPage;
    }
    AutoTagPage* autoTagPage() const
    {
        return m_autoTagPage;
    }
    TagEditorPage* tagEditorPage() const
    {
        return m_tagEditorPage;
    }
    BatchEditPage* batchEditPage() const
    {
        return m_batchEditPage;
    }
    CollectorPage* collectorPage() const
    {
        return m_collectorPage;
    }

protected:
    void showEvent(QShowEvent* event) override;

private:
    core::FacetIndex* m_facets = nullptr;
    core::AutoTaggerLibrary* m_taggerLibrary = nullptr;
    utils::AppSettings* m_settings = nullptr;

    TagClusterPage* m_tagClusterPage = nullptr;
    AutoTagPage* m_autoTagPage = nullptr;
    TagEditorPage* m_tagEditorPage = nullptr;
    BatchEditPage* m_batchEditPage = nullptr;
    CollectorPage* m_collectorPage = nullptr;

    QHBoxLayout* m_tabLayout;
    QStackedWidget* m_stack;
    QButtonGroup* m_tabGroup;
    QWidget* m_tabBar = nullptr;
    QWidget* m_indicator = nullptr;
    QPointer<QPropertyAnimation> m_indicatorAnim;
    bool m_indicatorPlaced = false;

    void addTab(const QString& label, QWidget* page);
    void moveIndicatorTo(QWidget* btn, bool animate);
};

} // namespace gui
