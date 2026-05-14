#pragma once
#include <core/clusterfilter.h>
#include <QWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QSlider>
#include <QCheckBox>
#include <QRadioButton>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QHash>
#include <QList>
#include <QPair>
#include <QPixmap>

class QNetworkAccessManager;
class QNetworkReply;
class QJsonArray;
class QTimer;

namespace core {
class FacetIndex;
class DanbooruIndex;
}

namespace gui {

class TagLineAutocomplete;

class TagClusterPage : public QWidget {
    Q_OBJECT
public:
    explicit TagClusterPage(core::FacetIndex* facets, QWidget* parent = nullptr);

    // Set once the async danbooru.csv load finishes (see AppMainWindow). The
    // PMI baseline reads per-tag post counts from this index, so until it
    // arrives the results panel stays empty.
    void setDanbooruIndex(core::DanbooruIndex* index);

    // Quick-facet menu wiring - same names AppMainWindow uses for the composer
    // and tile view. Empty string disables that quick-add entry.
    void setQuickFacets(const QString& character, const QString& copyright,
                        const QString& triggerWord, const QString& style);

    // Called by AppMainWindow::reloadFacets when the facet index changes
    // (quick-add, facet editor save, etc.) so the result rows reflect the
    // new facet badges and the facet filter is re-applied. No-op until a
    // fetch has produced data.
    void refreshFacets();

signals:
    void wikiRequested(const QString& tag);
    void facetEditorRequested(const QString& tag);
    void quickFacetRequested(const QString& tag, const QString& facetName);
    // Emitted by the "Create entry" button. AppMainWindow owns the
    // EntryModel, so it does the actual creation, persists, and selects
    // the new entry in the tile view.
    void createEntryRequested(const QString& title, const QStringList& tags);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    core::FacetIndex* m_facets = nullptr;
    core::DanbooruIndex* m_danbooru = nullptr; // PMI baseline (per-tag post counts)

    // ---- Params (left panel)
    QLineEdit* m_tagInput;
    TagLineAutocomplete* m_tagAutocomplete = nullptr; // attached once the index loads
    QCheckBox* m_soloCheck;
    QCheckBox* m_singleCharCheck;
    QSpinBox* m_charPagesSpin;
    QSlider* m_minPctSlider;   // min share of fetched posts a tag must appear in
    QLabel* m_minPctValueLbl;
    QSlider* m_minPmiSlider;
    QLabel* m_minPmiValueLbl;
    QPushButton* m_fetchBtn;
    QPushButton* m_cancelBtn;

    // ---- Filter editor
    QRadioButton* m_blacklistRadio;
    QRadioButton* m_whitelistRadio;
    QPlainTextEdit* m_filterEdit;
    QPushButton* m_saveFilterBtn;
    QLabel* m_filterStatusLbl;

    // ---- Status (results panel)
    QLabel* m_statusLabel;
    QProgressBar* m_progressBar;

    // ---- Results (results panel)
    QWidget* m_resultsContainer;
    QVBoxLayout* m_resultsLayout;
    QWidget* m_resultsScroll = nullptr; // stacks with m_emptyState - only one visible
    QWidget* m_emptyState = nullptr;
    QLabel* m_emptyStateLbl = nullptr;
    QPlainTextEdit* m_copyEdit;
    QPushButton* m_copyBtn;
    QPushButton* m_createEntryBtn;

    // ---- Preview (right panel)
    QLabel* m_previewImage;
    QLabel* m_previewStatus;
    int m_previewPostId = -1;
    QString m_previewForTag;

    // ---- Network
    QNetworkAccessManager* m_nam;
    QNetworkReply* m_pageReply = nullptr; // in-flight posts.json request, if any

    // ---- Fetch state
    enum class Phase { Idle, CharFetch };
    Phase m_phase = Phase::Idle;
    int m_currentPage = 0;
    int m_charPages = 0;
    // Bumped on every Fetch and on Cancel; in-flight replies / page timers
    // captured the old value and bail when it no longer matches.
    int m_fetchGen = 0;
    QString m_targetTag;
    bool m_fetchedSolo = false;
    bool m_fetchedSingleChar = false;

    // ---- Per-fetch counters (live for the session, rebuilt on each Fetch).
    // m_charCounter is a document frequency: a post lists each general tag
    // once, so the value is "how many of the character's posts carry this tag".
    QHash<QString, int> m_charCounter;
    QHash<QString, int> m_copyrightCounter;
    int m_charPostCount = 0;  // posts that contributed to m_charCounter
    int m_fetchedPostCount = 0; // posts pulled from the API (>= m_charPostCount)
    bool m_charDataReady = false;

    // ---- Result rows
    struct ResultRow {
        QWidget* widget;
        QString tag;
        bool included;
    };
    QList<ResultRow> m_rows;
    QString m_copyright;

    // ---- Quick-facet config (set by AppMainWindow)
    QString m_quickCharFacet;
    QString m_quickCopyFacet;
    QString m_quickTriggerFacet;
    QString m_quickStyleFacet;

    // ---- Debounced recompute (slider drags trigger many changes per second)
    QTimer* m_recomputeTimer = nullptr;

    // ---- Methods
    void onFetchClicked();
    void cancelFetch();
    void fetchNextPage(int gen);
    void processPage(const QJsonArray& posts);
    void onPhaseDone();
    void recompute();         // re-applies filters/threshold to cached counters
    void scheduleRecompute(); // collapses bursts of slider/edit changes
    void rebuildCopyString();
    QWidget* makeResultRow(const QString& tag, double pmi, int idx);
    void installRowContextMenu(QWidget* w, const QString& tag);
    void clearResultRows();

    // Filter persistence
    void loadFilters(); // reads from data/system/cluster_filters.fct
    void saveFilters(); // writes back; called explicitly via the button
    core::ClusterFilter buildFilterFromEditor() const;

    // Preview chain mirrors FacetEditorPage: wiki page first, falls back to first post.
    void clearPreview();
    void fetchPreview(const QString& tag);
    void fetchPostById(const QString& tag, int postId);
    void fetchFirstPostByTag(const QString& tag);
    void fetchPreviewImage(const QString& tag, const QString& imageUrl);
    void setPreviewPixmap(const QPixmap& pix);

    // UI helpers
    void setStatus(const QString& msg);
    void setProgress(int cur, int total);
    void setFetchRunning(bool on);
    void markStaleIfFetched();        // called when +solo flips after a fetch
    void setResultsEmpty(bool empty); // toggles centered placeholder vs scroll
};

} // namespace gui
