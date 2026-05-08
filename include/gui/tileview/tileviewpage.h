#pragma once
#include <core/entrymodel.h>
#include <core/entry.h>
#include <core/danbooruindex.h>
#include <QColor>
#include <QWidget>
#include <QLineEdit>
#include <QTimer>
#include <QBoxLayout>
#include <QResizeEvent>
#include <QMap>

namespace core {
class ComfyUiClient;
class FacetIndex;
}

namespace gui {
class EntryView;
class EntryPanel;

class TileViewPage : public QWidget {
    Q_OBJECT
public:
    explicit TileViewPage(core::EntryModel* model, QWidget* parent = nullptr);

    void setDanbooruIndex(core::DanbooruIndex* index);
    void setFacetIndex(core::FacetIndex* index);
    void refreshTags();
    void setActiveGroups(const QMap<int, QList<int>>& groups);
    void setLoraActiveByUuids(const QList<QString>& uuids);
    void setLoraDirs(const QString& primaryDir, const QString& testDir);
    void setLoraDefaults(double modelStr, double clipStr);
    void setComfyClient(core::ComfyUiClient* client);
    void setQuickFacets(const QString& characterFacet, const QString& copyrightFacet,
                        const QString& triggerWordFacet, const QString& styleFacet);
    void setTileGradient(qreal start, int alpha);
    void setTileTitleColor(const QColor& color);
    QList<QString> activeLoraUuids() const;
    // Re-runs the current search-bar query; used post-import to surface
    // newly-added entries without losing typed state.
    void refreshEntries();
    // Centers the entry in the view and selects it (entry must be in the
    // current filter result set; call refreshEntries first if just added).
    void selectEntry(int32_t entryId);

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

    EntryView* m_entryView;
    EntryPanel* m_entryPanel;
    QLineEdit* m_searchBar;
    QTimer* m_debounce;
    QBoxLayout* m_rootLayout;
};

} // namespace gui
