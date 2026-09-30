#pragma once
#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>

class QGraphicsOpacityEffect;
class QLabel;
class QLineEdit;
class QListWidget;
class QPropertyAnimation;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTextBrowser;
class QVBoxLayout;

namespace tc {

class AppData;
class FlowLayout;
class TagPreviewFetcher;

// Assigns facets to tags: tag list, facet pills per category, Danbooru preview.
class FacetEditorPage : public QWidget {
    Q_OBJECT

public:
    explicit FacetEditorPage(AppData& data, QWidget* parent = nullptr);

    // Rebuilds the tag list from the entries plus every defined tag.
    void reload();

    // Composer tags for the undefined list; read on every show.
    void setActiveTagsProvider(std::function<QStringList()> provider);

public slots:
    void selectTagByName(const QString& tag);
    void refreshUndefinedList();

signals:
    void facetsDefined();
    void wikiRequested(const QString& tag);
    void composerRequested();
    void statusMessage(const QString& message);

protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

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

    // Right rail rendering; TagPreviewFetcher does the lookups.
    void setPreviewPixmap(const QPixmap& image);
    void clearPreview();
    void setWikiBody(const QString& body);

    AppData* m_data = nullptr;
    QString m_selectedTag;
    std::function<QStringList()> m_activeTagsProvider;

    // Left
    QLineEdit* m_searchEdit = nullptr;
    QLabel* m_undefinedHeader = nullptr;
    QListWidget* m_undefinedList = nullptr;
    QListWidget* m_tagList = nullptr;
    QLabel* m_countLabel = nullptr;

    // Middle
    QLabel* m_selectedLabel = nullptr;
    QLineEdit* m_facetSearchEdit = nullptr;
    QWidget* m_activePillsHost = nullptr;
    FlowLayout* m_activePillsFlow = nullptr;
    QScrollArea* m_facetsScroll = nullptr;
    QWidget* m_facetsContainer = nullptr;
    QVBoxLayout* m_facetsLayout = nullptr;
    QPushButton* m_saveBtn = nullptr;
    QStackedWidget* m_rightStack = nullptr;

    // Right
    TagPreviewFetcher* m_preview = nullptr;
    QWidget* m_previewPanel = nullptr;
    QLabel* m_previewImage = nullptr;
    QLabel* m_previewStatus = nullptr;
    QGraphicsOpacityEffect* m_previewFade = nullptr;
    QPropertyAnimation* m_previewFadeAnim = nullptr;
    QLabel* m_wikiHeader = nullptr;
    QTextBrowser* m_wikiText = nullptr;
    int m_previewPostId = -1; // for the click-through to the post page
};

} // namespace tc
