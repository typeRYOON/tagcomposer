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
class QJsonArray;
class QTimer;

namespace core {
class FacetIndex;
}

namespace gui {

class TagClusterPage : public QWidget {
    Q_OBJECT
public:
    explicit TagClusterPage(core::FacetIndex* facets, QWidget* parent = nullptr);

    // Quick-facet menu wiring - same names AppMainWindow uses for the composer
    // and tile view. Empty string disables that quick-add entry.
    void setQuickFacets(const QString& character, const QString& copyright,
                        const QString& triggerWord, const QString& style);

signals:
    void wikiRequested(const QString& tag);
    void facetEditorRequested(const QString& tag);
    void quickFacetRequested(const QString& tag, const QString& facetName);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    core::FacetIndex* m_facets = nullptr;

    // ── Params (left panel) ──────────────────────────────────────────────────
    QLineEdit* m_tagInput;
    QCheckBox* m_soloCheck;
    QSpinBox* m_charPagesSpin;
    QSpinBox* m_globalPagesSpin;
    QSlider* m_minPmiSlider;
    QLabel* m_minPmiValueLbl;
    QSpinBox* m_minCountSpin;
    QPushButton* m_fetchBtn;
    QPushButton* m_clearCacheBtn;

    // ── Filter editor ────────────────────────────────────────────────────────
    QRadioButton* m_blacklistRadio;
    QRadioButton* m_whitelistRadio;
    QPlainTextEdit* m_filterEdit;
    QPushButton* m_saveFilterBtn;
    QLabel* m_filterStatusLbl;

    // ── Status (results panel) ───────────────────────────────────────────────
    QLabel* m_statusLabel;
    QProgressBar* m_progressBar;

    // ── Results (results panel) ──────────────────────────────────────────────
    QWidget* m_resultsContainer;
    QVBoxLayout* m_resultsLayout;
    QWidget* m_resultsScroll = nullptr; // stacks with m_emptyState - only one visible
    QWidget* m_emptyState = nullptr;
    QLabel* m_emptyStateLbl = nullptr;
    QPlainTextEdit* m_copyEdit;
    QPushButton* m_copyBtn;

    // ── Preview (right panel) ────────────────────────────────────────────────
    QLabel* m_previewImage;
    QLabel* m_previewStatus;
    int m_previewPostId = -1;
    QString m_previewForTag;

    // ── Network ──────────────────────────────────────────────────────────────
    QNetworkAccessManager* m_nam;

    // ── Fetch state ──────────────────────────────────────────────────────────
    enum class Phase { Idle, GlobalFetch, CharFetch };
    Phase m_phase = Phase::Idle;
    int m_currentPage = 0;
    int m_charPages = 0;
    int m_globalPages = 0;
    QString m_targetTag;
    bool m_fetchedSolo = false;

    // ── Cached counters (live for the session) ───────────────────────────────
    QHash<QString, int> m_globalCounter;
    qint64 m_globalTotal = 0;

    QHash<QString, int> m_charCounter;
    QHash<QString, int> m_copyrightCounter;
    qint64 m_charTotal = 0;
    bool m_charDataReady = false;

    // ── Result rows ──────────────────────────────────────────────────────────
    struct ResultRow {
        QWidget* widget;
        QString tag;
        bool included;
    };
    QList<ResultRow> m_rows;
    QString m_copyright;

    // ── Quick-facet config (set by AppMainWindow) ────────────────────────────
    QString m_quickCharFacet;
    QString m_quickCopyFacet;
    QString m_quickTriggerFacet;
    QString m_quickStyleFacet;

    // ── Debounced recompute (slider drags trigger many changes per second) ──
    QTimer* m_recomputeTimer = nullptr;

    // ── Methods ──────────────────────────────────────────────────────────────
    void onFetchClicked();
    void fetchNextPage();
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

    // Global cache (the only thing that persists across sessions)
    bool hasCachedGlobal() const;
    void loadGlobalCache();
    void saveGlobalCache();
    QString cachePath() const;

    // Preview chain - mirrors FacetEditorPage's pattern (wiki → first post).
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
