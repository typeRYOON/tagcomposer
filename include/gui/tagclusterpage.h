#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QHash>
#include <QList>
#include <QPair>

class QNetworkAccessManager;
class QJsonArray;

namespace gui {

class TagClusterPage : public QWidget {
    Q_OBJECT
public:
    explicit TagClusterPage(QWidget* parent = nullptr);

private:
    // ── Params ────────────────────────────────────────────────────────────────
    QLineEdit*      m_tagInput;
    QSpinBox*       m_charPagesSpin;
    QSpinBox*       m_globalPagesSpin;
    QSpinBox*       m_topNSpin;
    QDoubleSpinBox* m_minFreqSpin;
    QDoubleSpinBox* m_minPmiSpin;
    QDoubleSpinBox* m_alphaSpin;
    QPlainTextEdit* m_excludeEdit;
    QPushButton*    m_runBtn;
    QPushButton*    m_clearCacheBtn;

    // ── Status ────────────────────────────────────────────────────────────────
    QLabel*       m_statusLabel;
    QProgressBar* m_progressBar;

    // ── Results ───────────────────────────────────────────────────────────────
    QWidget*        m_resultsContainer;
    QVBoxLayout*    m_resultsLayout;
    QPlainTextEdit* m_copyEdit;
    QPushButton*    m_copyBtn;

    // ── Network ───────────────────────────────────────────────────────────────
    QNetworkAccessManager* m_nam;

    // ── State ─────────────────────────────────────────────────────────────────
    enum class Phase { Idle, GlobalFetch, CharFetch };
    Phase   m_phase       = Phase::Idle;
    int     m_currentPage = 0;
    int     m_charPages   = 0;
    int     m_globalPages = 0;
    QString m_targetTag;

    // ── Accumulated data ──────────────────────────────────────────────────────
    QHash<QString, int> m_globalCounter;
    qint64              m_globalTotal = 0;

    QHash<QString, int> m_charCounter;
    QHash<QString, int> m_copyrightCounter;
    qint64              m_charTotal  = 0;

    // ── Result rows ───────────────────────────────────────────────────────────
    struct ResultRow { QWidget* widget; QString tag; bool included; };
    QList<ResultRow> m_rows;
    QString          m_copyright;

    // ── Methods ───────────────────────────────────────────────────────────────
    void onRunClicked();
    void fetchNextPage();
    void processPage(const QJsonArray& posts);
    void onPhaseDone();
    void computeAndDisplay();
    void rebuildCopyString();
    QWidget* makeResultRow(const QString& tag, double score, int idx);

    bool    hasCachedGlobal() const;
    void    loadGlobalCache();
    void    saveGlobalCache();
    QString cachePath() const;

    void setStatus(const QString& msg);
    void setProgress(int cur, int total);
    void setRunning(bool on);
};

} // namespace gui
