#pragma once
#include <core/phashindex.h>
#include <QObject>
#include <QString>
#include <QSet>

class QTimer;

namespace core {

// Watches a single source folder ("downloads") and routes new image files
// into a single named collection folder. On each poll tick: list candidates,
// hash, dedup against the collection's PHashIndex (Hamming threshold), then
// either move-to-recycle-bin (duplicate) or rename-and-move with a sequential
// "NNNNN.<ext>" name.
//
// Lifetime: only one Watcher should be running at a time across the app -
// otherwise two tickers would race for the same source files. The Collector
// page enforces that by owning a single instance.
class DownloadWatcher : public QObject {
    Q_OBJECT
public:
    explicit DownloadWatcher(QObject* parent = nullptr);
    ~DownloadWatcher() override;

    // Spins up the timer. Re-loads the destination's PHashIndex from disk
    // so the watcher picks up wherever the user left off.
    void start(const QString& watchFolder,
               const QString& collectionDir,
               int            hammingThreshold,
               int            pollSeconds);

    // Stops the timer immediately. Safe to call when not running.
    void stop();

    bool isRunning() const { return m_active; }

    // Counters reset to zero on every start().
    int collectedCount() const { return m_collected; }
    int skippedCount()   const { return m_skipped; }

signals:
    void started();
    void stopped();
    void imageMoved(QString fromAbs, QString toAbs);
    void imageSkipped(QString fromAbs, int distance, QString matchName);
    void error(QString message);
    void status(int collected, int skipped);

private:
    void poll();

    QTimer*    m_timer       = nullptr;
    bool       m_active      = false;
    QString    m_watchFolder;
    QString    m_collectionDir;
    int        m_threshold   = 4;
    PHashIndex m_index;
    int        m_collected   = 0;
    int        m_skipped     = 0;
};

} // namespace core
