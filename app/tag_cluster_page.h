#pragma once
#include <QHash>
#include <QList>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QWidget>

class QCheckBox;
class QJsonArray;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSlider;
class QSpinBox;
class QTimer;
class QVBoxLayout;

namespace tc {

class ClusterFilter;
class DanbooruIndex;
class TagFacets;
class TagLineAutocomplete;
class TagPreviewFetcher;
class TagPreviewPopup;

// Pulls a character's posts from Danbooru and ranks the tags that co-occur
// with them by PMI, which is what separates "what this character looks like"
// from "what anime art looks like". The thresholds re-apply without
// re-fetching, so tuning a cluster costs nothing.
class TagClusterPage : public QWidget {
    Q_OBJECT

public:
    TagClusterPage(const TagFacets& facets, const QString& filtersPath,
                   QWidget* parent = nullptr);

    // The PMI baseline is the index's per-tag post counts, so until this
    // arrives there is nothing to score against and the results stay empty.
    void setDanbooruIndex(const DanbooruIndex* index);

    // Quick-add entries for the row menu, named as in the composer. An empty
    // string leaves that entry out.
    void setQuickFacets(const QString& character, const QString& copyright,
                        const QString& triggerWord, const QString& style);

    // The facet definitions changed under us, so the badges and the facet
    // filter both need another pass. A no-op before the first fetch.
    void refreshFacets();

signals:
    void wikiRequested(const QString& tag);
    void facetEditorRequested(const QString& tag);
    void quickFacetRequested(const QString& tag, const QString& facet);
    void createEntryRequested(const QString& title, const QStringList& tags);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void startFetch();
    void cancelFetch();
    void fetchNextPage(int generation);
    void processPage(const QJsonArray& posts);
    void finishFetch();

    void recompute();
    void scheduleRecompute();
    void rebuildCopyString();
    void clearResultRows();
    QWidget* makeResultRow(const QString& tag, double pmi, qsizetype index);
    void installRowMenu(QWidget* widget, const QString& tag);
    TagPreviewPopup* previewPopup();

    ClusterFilter filterFromEditor() const;
    void loadFilters();
    void saveFilters();

    void clearPreview();
    void showPreview(const QPixmap& image);

    void setStatus(const QString& message);
    void setProgress(int current, int total);
    void setFetchRunning(bool running);
    void markStale();
    void setResultsEmpty(bool empty);

    const TagFacets* m_facets = nullptr;
    QString m_filtersPath; // system/cluster_filters.fct, resolved by the shell
    const DanbooruIndex* m_danbooru = nullptr;

    QLineEdit* m_tagInput = nullptr;
    TagLineAutocomplete* m_autocomplete = nullptr; // attached once the index lands
    QCheckBox* m_solo = nullptr;
    QCheckBox* m_singleChar = nullptr;
    QSpinBox* m_pages = nullptr;
    QSlider* m_minPct = nullptr;
    QLabel* m_minPctValue = nullptr;
    QSlider* m_minPmi = nullptr;
    QLabel* m_minPmiValue = nullptr;
    QPushButton* m_fetchBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;

    QRadioButton* m_blacklist = nullptr;
    QRadioButton* m_whitelist = nullptr;
    QPlainTextEdit* m_filterEdit = nullptr;
    QPushButton* m_saveFilterBtn = nullptr;
    QLabel* m_filterStatus = nullptr;

    QLabel* m_status = nullptr;
    QProgressBar* m_progress = nullptr;

    QWidget* m_resultsContainer = nullptr;
    QVBoxLayout* m_resultsLayout = nullptr;
    QWidget* m_resultsScroll = nullptr; // swaps with m_emptyState
    QWidget* m_emptyState = nullptr;
    QLabel* m_emptyLabel = nullptr;
    QPlainTextEdit* m_copyEdit = nullptr;
    QPushButton* m_copyBtn = nullptr;
    QPushButton* m_createEntryBtn = nullptr;

    TagPreviewFetcher* m_preview = nullptr;
    TagPreviewPopup* m_previewPopup = nullptr;
    QLabel* m_previewImage = nullptr;
    QLabel* m_previewStatus = nullptr;
    int m_previewPostId = -1;
    QString m_previewForTag;

    QNetworkAccessManager* m_network = nullptr;
    QNetworkReply* m_pageReply = nullptr;

    bool m_fetching = false;
    int m_currentPage = 0;
    int m_pageTarget = 0;

    // Bumped on every fetch and on cancel. An in-flight reply or a pending
    // inter-page timer captured the old value and bails when it no longer
    // matches, which is what makes cancel immediate.
    int m_generation = 0;

    QString m_targetTag;
    bool m_fetchedSolo = false;
    bool m_fetchedSingleChar = false;

    // Document frequency: a post lists each general tag once, so the value is
    // how many of this character's posts carry the tag.
    QHash<QString, int> m_tagCounts;
    QHash<QString, int> m_copyrightCounts;
    int m_usedPosts = 0;    // posts that contributed to m_tagCounts
    int m_fetchedPosts = 0; // posts pulled, which is >= m_usedPosts
    bool m_dataReady = false;

    struct ResultRow {
        QWidget* widget;
        QString tag;
        bool included;
    };
    QList<ResultRow> m_rows;
    QString m_copyright;

    QString m_quickCharacter;
    QString m_quickCopyright;
    QString m_quickTrigger;
    QString m_quickStyle;

    // A slider drag would otherwise rebuild every row many times a second.
    QTimer* m_recomputeTimer = nullptr;
};

} // namespace tc
