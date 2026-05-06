#pragma once
#include <core/entry.h>
#include <core/danbooruindex.h>
#include <core/entrymodel.h>
#include <gui/widgets/tagsearchbar.h>
#include <gui/tileview/imagedropper.h>
#include <gui/tileview/addnewentrydialog.h>

namespace core {
class ComfyUiClient;
class FacetIndex;
}
#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QBoxLayout>
#include <QScrollArea>
#include <QStackedWidget>
#include <QDoubleSpinBox>
#include <QSet>
#include <QMap>
#include <QTimer>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>

namespace gui {

class EntryPanel : public QWidget {
    Q_OBJECT
public:
    explicit EntryPanel(core::EntryModel* model, QWidget* parent = nullptr);

    void setEntry(core::Entry* entry);
    void setDanbooruIndex(core::DanbooruIndex* index);
    void setFacetIndex(core::FacetIndex* index);
    void setActiveGroups(const QMap<int, QList<int>>& groups);
    void setComfyClient(core::ComfyUiClient* client);
    void setQuickFacets(const QString& characterFacet, const QString& copyrightFacet,
                        const QString& triggerWordFacet, const QString& styleFacet);
    void refreshTags();
    void applyOrientation(bool portrait);
    // primary is the move-target for dropped files outside both roots;
    // test is read-only (recognised on drop, never written to). Either empty.
    void setLoraDirs(const QString& primaryDir, const QString& testDir);
    void setLoraDefaults(double modelStr, double clipStr);

signals:
    void entryListChanged();
    void entrySelectRequested(int32_t entryId);
    void entryModified(int32_t entryId);
    void loraCleared(int32_t entryId);
    void entryTagAdded(int32_t entryId, int imageIdx, const QString& tag);
    void entryTagRemoved(int32_t entryId, int imageIdx, const QString& tag);
    void entryImageChanged(int32_t entryId, int imageIdx, const QString& newPath);
    void tagAlreadyPresent(const QString& tag);
    void tagsExported(int entryId, int imageIdx, QList<QString> tags);
    void wikiRequested(const QString& tag);
    void facetEditorRequested(const QString& tag);
    void quickFacetRequested(const QString& tag, const QString& facetName);
    void statusMessageRequested(const QString& message);

private:
    void loadImagePage(int idx);
    void rebuildTagList();
    void updateExtraBtnState();
    void refreshLoraSection();
    QWidget* createTagRow(const QString& tag);
    void addTagRowToList(const QString& tag);
    // Synchronous content swap; setEntry wraps this with a fade transition.
    void applyEntry(core::Entry* entry);

    core::EntryModel* m_model;
    core::DanbooruIndex* m_danbooruIndex = nullptr;
    core::FacetIndex* m_facetIndex = nullptr;
    core::ComfyUiClient* m_comfyClient = nullptr;
    core::Entry* m_entry = nullptr;
    int m_imageIdx = 0;
    QSet<QString> m_activeTags;
    QMap<int, QList<int>> m_activeGroups;
    QString m_loraPrimaryDir;
    QString m_loraTestDir;
    double m_defaultLoraModelStr = 1.0;
    double m_defaultLoraClipStr = 1.0;
    QString m_quickCharFacet;
    QString m_quickCopyFacet;
    QString m_quickTriggerFacet;
    QString m_quickStyleFacet;

    // Layout
    QStackedWidget* m_stack;
    QWidget* m_contentWidget;
    QBoxLayout* m_rootLayout;
    QWidget* m_headerWidget;
    QWidget* m_tagsWidget;

    void addEmptyImageSlot();

    // Header
    ImageDropper* m_imageDrop;
    QLineEdit* m_titleEdit;
    QPushButton* m_composerBtn;
    QPushButton* m_copyBtn;
    QPushButton* m_deleteBtn;
    QPlainTextEdit* m_commentEdit;
    QTimer* m_commentTimer;
    QPushButton* m_prevBtn;
    QPushButton* m_nextBtn;
    QPushButton* m_addImageBtn;
    QPushButton* m_removeImageBtn;
    QLabel* m_pageLabel;

    // Tags
    TagSearchBar* m_searchBar;
    QScrollArea* m_tagScroll;
    QWidget* m_tagListContainer;
    QVBoxLayout* m_tagListLayout;

    // Hides the rebuild flicker when the tag list is rebuilt as a side effect
    // of a quick-add (facet write -> reloadFacets -> refreshTags). One-shot:
    // rebuildTagList consumes the flag.
    bool m_fadeNextTagRebuild = false;
    QGraphicsOpacityEffect* m_tagListFx = nullptr;
    QPropertyAnimation* m_tagListFade = nullptr;

    // Whole-panel crossfade between entries: snapshot of the previous entry
    // sits on top and fades out, revealing the new content underneath. This
    // avoids stacking a QGraphicsOpacityEffect on a parent of a QScrollArea,
    // which leaves children mis-laid-out until a hover repaint.
    QLabel* m_fadeOverlay = nullptr;
    QGraphicsOpacityEffect* m_fadeOverlayFx = nullptr;
    QPropertyAnimation* m_fadeOverlayAnim = nullptr;

    // LoRA section
    QWidget* m_loraSection = nullptr;
    QLabel* m_loraFileLabel = nullptr;
    QPushButton* m_loraClearBtn = nullptr;
    QPushButton* m_loraSha256Btn = nullptr;
    QDoubleSpinBox* m_loraModelStr = nullptr;
    QDoubleSpinBox* m_loraClipStr = nullptr;
    QTimer* m_loraTimer = nullptr;
};

} // namespace gui
