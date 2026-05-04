#include <core/downloadwatcher.h>
#include <core/phasher.h>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

namespace core {

namespace {
const QStringList kImageExtensions = {
    "*.png", "*.jpg", "*.jpeg", "*.webp", "*.bmp", "*.gif"
};
}

DownloadWatcher::DownloadWatcher(QObject* parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, &DownloadWatcher::poll);
}

DownloadWatcher::~DownloadWatcher()
{
    stop();
}

void DownloadWatcher::start(const QString& watchFolder,
                            const QString& collectionDir,
                            int            hammingThreshold,
                            int            pollSeconds)
{
    if (m_active) return;
    if (watchFolder.isEmpty() || collectionDir.isEmpty()) {
        emit error("Watch folder and collection are required.");
        return;
    }
    if (!QDir(watchFolder).exists()) {
        emit error(QString("Watch folder doesn't exist: %1").arg(watchFolder));
        return;
    }

    m_watchFolder   = watchFolder;
    m_collectionDir = collectionDir;
    m_threshold     = hammingThreshold;
    m_collected     = 0;
    m_skipped       = 0;
    m_index         = PHashIndex::loadFromDir(collectionDir);

    QDir().mkpath(collectionDir);

    m_timer->start(qMax(1, pollSeconds) * 1000);
    m_active = true;
    emit started();

    // Immediate first tick so the user sees activity without waiting a full
    // poll interval after pressing Start.
    poll();
}

void DownloadWatcher::stop()
{
    if (!m_active) return;
    m_timer->stop();
    m_active = false;
    // Persist any in-memory updates one last time. add() inside poll()
    // already saves after each move, so this is mostly belt-and-braces.
    m_index.saveToDir(m_collectionDir);
    emit stopped();
}

void DownloadWatcher::poll()
{
    if (!m_active) return;

    // Discover candidates. We don't recurse - the Python reference watches a
    // flat downloads folder, and recursing would risk pulling files out of
    // unrelated subdirectories the user hasn't opted in.
    QStringList candidates;
    for (const QString& pat : kImageExtensions) {
        QDirIterator it(m_watchFolder, { pat }, QDir::Files);
        while (it.hasNext()) candidates << it.next();
    }
    std::sort(candidates.begin(), candidates.end());  // deterministic order

    for (const QString& src : candidates) {
        const QFileInfo fi(src);

        // Skip files still being downloaded. Two heuristics:
        //   1. Browser/wget/etc. typically write to a sibling .part file
        //      (or the same name with a .part suffix) until the transfer
        //      finishes - wait until that's gone.
        //   2. A 0-byte file is mid-create (or a corruption); skip and let
        //      the next tick find it once it has bytes.
        if (QFile::exists(src + ".part")) continue;
        if (fi.size() == 0)               continue;

        const uint64_t hash = phashFile(src);
        if (hash == 0) {
            emit error(QString("phash failed: %1").arg(fi.fileName()));
            continue;
        }

        // Dedup check against the collection. First match within threshold
        // wins; the Python reference does the same via a flat set lookup.
        if (auto m = m_index.findNearest(hash, m_threshold);
            !m.filename.isEmpty())
        {
            if (!QFile::moveToTrash(src)) {
                emit error(QString("Recycle-bin failed for %1").arg(fi.fileName()));
                continue;
            }
            ++m_skipped;
            emit imageSkipped(src, m.distance, m.filename);
            emit status(m_collected, m_skipped);
            continue;
        }

        // New image - allocate the next sequential number, rename + move.
        const int n = m_index.nextNumber();
        const QString destName = QString("%1.%2")
            .arg(n, 5, 10, QChar('0'))
            .arg(fi.suffix().toLower());
        const QString destPath = m_collectionDir + "/" + destName;

        if (!QFile::rename(src, destPath)) {
            emit error(QString("Move failed: %1 → %2").arg(fi.fileName(), destName));
            continue;
        }

        m_index.add(destName, hash);
        // Save after every successful move so a crash/kill doesn't lose
        // index state. The JSON is small (16-char hex per entry); writing
        // it on each move is cheap even at thousands of entries.
        m_index.saveToDir(m_collectionDir);
        ++m_collected;
        emit imageMoved(src, destPath);
        emit status(m_collected, m_skipped);
    }
}

} // namespace core
