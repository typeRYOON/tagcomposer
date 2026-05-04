#pragma once
#include <core/phashindex.h>
#include <QObject>
#include <QString>
#include <QSet>

class QTimer;

namespace core {

// Polls a source folder, dedups against a collection's PHashIndex, then
// recycles duplicates or moves new images in as "NNNNN.<ext>".
// One instance only - concurrent watchers race over the same files.
class DownloadWatcher : public QObject {
    Q_OBJECT
public:
    explicit DownloadWatcher(QObject* parent = nullptr);
    ~DownloadWatcher() override;

    // Reloads the destination's PHashIndex; counters reset to zero.
    void start(const QString& watchFolder, const QString& collectionDir, int hammingThreshold,
               int pollSeconds);

    void stop();

    bool isRunning() const
    {
        return m_active;
    }
    int collectedCount() const
    {
        return m_collected;
    }
    int skippedCount() const
    {
        return m_skipped;
    }

signals:
    void started();
    void stopped();
    void imageMoved(QString fromAbs, QString toAbs);
    void imageSkipped(QString fromAbs, int distance, QString matchName);
    void error(QString message);
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

} // namespace core
