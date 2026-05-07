#pragma once
#include <core/facetindex.h>
#include <core/entrymodel.h>
#include <core/variableindex.h>
#include <core/danbooruindex.h>
#include <QWidget>
#include <QListWidget>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QStackedWidget>
#include <QHash>
#include <QPixmap>
#include <QNetworkAccessManager>
#include <functional>

QT_BEGIN_NAMESPACE
class QGraphicsOpacityEffect;
class QPropertyAnimation;
QT_END_NAMESPACE

namespace gui {

class FlowLayout;

class FacetEditorPage : public QWidget {
    Q_OBJECT
public:
    explicit FacetEditorPage(core::FacetIndex* facets, core::EntryModel* model,
                             QWidget* parent = nullptr);

    void reload();

    // Source of the composer's currently-active tag list. Pulled on every
    // show of this page so the "undefined in composer" list stays fresh
    // without needing to plumb a signal through the composer.
    void setActiveTagsProvider(std::function<QList<QString>()> provider);
    void setVariableIndex(core::VariableIndex* vars)
    {
        m_varIndex = vars;
    }
    void setDanbooruIndex(core::DanbooruIndex* index)
    {
        m_danbooruIndex = index;
    }

public slots:
    void selectTagByName(const QString& tag);
    void refreshUndefinedList();

signals:
    void facetsDefined();
    void wikiRequested(const QString& tag);
    // User clicked the schema reload button. Handled by AppMainWindow which
    // re-reads facets.fct into the shared FacetIndex and triggers reloadFacets.
    void schemaReloadRequested();
    // Emitted when the user finishes defining the last "undefined in composer"
    // tag; AppMainWindow switches back to the composer page.
    void composerRequested();

protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    void selectTag(const QString& tag);

private:
    void saveSelected();
    void clearEditor();
    void applyListFilter(const QString& query);
    void applyFacetFilter(const QString& query);
    void refreshActivePills();
    void focusFirstPill();
    QPushButton* neighborPill(QPushButton* current, int key) const;

    // Right-rail Danbooru preview. Tries the wiki page's first !post #N first
    // (matches what the in-app wiki page surfaces), falls back to the top hit
    // from /posts.json?tags=... if the wiki has no embedded post.
    void fetchPreview(const QString& tag);
    void fetchFirstPostByTag(const QString& tag);
    void fetchPostById(const QString& tag, int postId);
    void fetchPreviewImage(const QString& tag, const QString& imageUrl);
    void setPreviewPixmap(const QPixmap& pix);
    void clearPreview();

    core::FacetIndex* m_facets;
    core::EntryModel* m_model;
    core::VariableIndex* m_varIndex = nullptr;
    core::DanbooruIndex* m_danbooruIndex = nullptr;
    QString m_selectedTag;

    std::function<QList<QString>()> m_activeTagsProvider;

    // Left panel
    QLineEdit* m_searchEdit;
    QLabel* m_undefinedHeader;
    QListWidget* m_undefinedList;
    QListWidget* m_tagList;
    QLabel* m_countLabel;

    // Right panel
    QLabel* m_selectedLabel;
    QLineEdit* m_facetSearchEdit;
    QWidget* m_activePillsHost = nullptr;
    FlowLayout* m_activePillsFlow = nullptr;
    QScrollArea* m_facetsScroll = nullptr;
    QWidget* m_facetsContainer;
    QVBoxLayout* m_facetsLayout;
    QPushButton* m_saveBtn;
    QStackedWidget* m_rightStack;

    // Danbooru preview rail
    QNetworkAccessManager* m_nam = nullptr;
    QWidget* m_previewPanel = nullptr;
    QLabel* m_previewImage = nullptr;
    QLabel* m_previewStatus = nullptr;
    QGraphicsOpacityEffect* m_previewFade = nullptr;
    QPropertyAnimation* m_previewFadeAnim = nullptr;
    QHash<QString, QPixmap> m_previewCache;
    QHash<QString, int> m_previewPostIds; // tag -> post id (for click-through)
    int m_previewPostId = -1;
    // Bumped on every fetchPreview call. In-flight network chains capture
    // this and bail when it changes, so re-clicking the same tag mid-chain
    // can't end with two setPreviewPixmap calls (double fade).
    quint64 m_previewGen = 0;
};

} // namespace gui
