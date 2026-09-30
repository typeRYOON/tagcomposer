#pragma once
#include <tagger/phash_index.h>
#include <QObject>
#include <QString>

class QTimer;

namespace tc {

// Watches a download folder, and for each image that turns up either moves it
// into the collection as "NNNNN.<ext>" or recycles it as a near-duplicate of
// something already there.
//
// One instance at a time: two watchers over the same folder race each other
// for the same files.
class DownloadWatcher : public QObject {
    Q_OBJECT

public:
    explicit DownloadWatcher(QObject* parent = nullptr);
    ~DownloadWatcher() override;

    // Re-reads the collection's hash index and zeroes the counters.
    void start(const QString& watchFolder, const QString& collectionDir, int hammingThreshold,
               int pollSeconds);
    void stop();

    bool isRunning() const;
    int collectedCount() const;
    int skippedCount() const;

signals:
    void started();
    void stopped();
    void imageMoved(const QString& from, const QString& to);
    void imageSkipped(const QString& from, int distance, const QString& matchName);
    void error(const QString& message);
    void status(int collected, int skipped);

private:
    void poll();

    QTimer* m_timer = nullptr;
    bool m_active = false;

    QString m_watchFolder;
    QString m_collectionDir;
    int m_threshold = 4;

    PHashIndex m_index;
    int m_collected = 0;
    int m_skipped = 0;
};

} // namespace tc
