#pragma once
#include <core/entrymodel.h>
#include <core/entry.h>
#include <core/danbooruindex.h>
#include <QWidget>
#include <QLineEdit>
#include <QTimer>
#include <QBoxLayout>
#include <QResizeEvent>
#include <QMap>

namespace core { class ComfyUiClient; }

namespace gui {
class EntryView;
class EntryPanel;

class TileViewPage : public QWidget {
    Q_OBJECT
public:
    explicit TileViewPage(core::EntryModel* model, QWidget* parent = nullptr);

    void setDanbooruIndex(core::DanbooruIndex* index);
    void setActiveGroups(const QMap<int, QList<int>>& groups);
    void setLoraActiveByUuids(const QList<QString>& uuids);
    void setLoraBaseDir(const QString& dir);
    void setComfyClient(core::ComfyUiClient* client);
    void setQuickFacets(const QString& characterFacet,
                        const QString& copyrightFacet,
                        const QString& triggerWordFacet,
                        const QString& styleFacet);
    QList<QString> activeLoraUuids() const;
    // Re-runs the current search-bar query against the entry model. Used after
    // an import to surface newly-added entries without losing typed state.
    void refreshEntries();

signals:
    void tagsExported(int entryId, int imageIdx, QList<QString> tags);
    void entryTagAdded(int entryId, int imageIdx, const QString& tag);
    void entryTagRemoved(int entryId, int imageIdx, const QString& tag);
    void wikiRequested(const QString& tag);
    void facetEditorRequested(const QString& tag);
    void quickFacetRequested(const QString& tag, const QString& facetName);
    void loraStackChanged(QList<core::LoraConfig> stack);
    void statusMessageRequested(const QString& message);

private:
    void applyOrientation(bool portrait);

    EntryView*   m_entryView;
    EntryPanel*  m_entryPanel;
    QLineEdit*   m_searchBar;
    QTimer*      m_debounce;
    QBoxLayout*  m_rootLayout;
};

} // namespace gui
