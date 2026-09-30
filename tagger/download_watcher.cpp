#include <tagger/download_watcher.h>
#include <tagger/phash.h>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTimer>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

const QStringList kImageFilters = {u"*.png"_s, u"*.jpg"_s,  u"*.jpeg"_s,
                                   u"*.webp"_s, u"*.bmp"_s, u"*.gif"_s};

} // namespace

DownloadWatcher::DownloadWatcher(QObject* parent) : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, &DownloadWatcher::poll);
}

DownloadWatcher::~DownloadWatcher()
{
    stop();
}

bool DownloadWatcher::isRunning() const
{
    return m_active;
}

int DownloadWatcher::collectedCount() const
{
    return m_collected;
}

int DownloadWatcher::skippedCount() const
{
    return m_skipped;
}

void DownloadWatcher::start(const QString& watchFolder, const QString& collectionDir,
                            int hammingThreshold, int pollSeconds)
{
    if (m_active) return;

    if (watchFolder.isEmpty() || collectionDir.isEmpty()) {
        emit error(u"Watch folder and collection are both required."_s);
        return;
    }
    if (!QDir(watchFolder).exists()) {
        emit error(u"Watch folder does not exist: %1"_s.arg(watchFolder));
        return;
    }

    m_watchFolder = watchFolder;
    m_collectionDir = collectionDir;
    m_threshold = hammingThreshold;
    m_collected = 0;
    m_skipped = 0;
    m_index = PHashIndex::load(collectionDir);

    QDir().mkpath(collectionDir);

    m_timer->start(std::max(1, pollSeconds) * 1000);
    m_active = true;
    emit started();

    // Straight away rather than after the first interval, so pressing Start
    // on a folder that already has files does something visible.
    poll();
}

void DownloadWatcher::stop()
{
    if (!m_active) return;

    m_timer->stop();
    m_active = false;
    m_index.save(m_collectionDir);
    emit stopped();
}

void DownloadWatcher::poll()
{
    if (!m_active) return;

    // Not recursive: a browser's download folder has unrelated subdirectories
    // and none of them are ours to move files out of.
    QStringList candidates;
    for (const QString& filter : kImageFilters) {
        QDirIterator it(m_watchFolder, {filter}, QDir::Files);
        while (it.hasNext()) candidates << it.next();
    }
    std::sort(candidates.begin(), candidates.end());

    for (const QString& source : candidates) {
        const QFileInfo info(source);

        // Still downloading. A browser writes a .part beside the target and
        // leaves the target empty until it finishes, and hashing either one
        // gives a hash for a file that is about to change.
        if (QFile::exists(source + u".part"_s)) continue;
        if (info.size() == 0) continue;

        const uint64_t hash = phashFile(source);
        if (hash == 0) {
            emit error(u"pHash failed: %1"_s.arg(info.fileName()));
            continue;
        }

        if (const PHashIndex::Match match = m_index.findNearest(hash, m_threshold);
            !match.filename.isEmpty()) {
            if (!QFile::moveToTrash(source)) {
                emit error(u"Recycle bin refused %1"_s.arg(info.fileName()));
                continue;
            }
            ++m_skipped;
            emit imageSkipped(source, match.distance, match.filename);
            emit status(m_collected, m_skipped);
            continue;
        }

        const QString name = u"%1.%2"_s.arg(m_index.nextNumber(), 5, 10, QChar(u'0'))
                                 .arg(info.suffix().toLower());
        const QString destination = m_collectionDir + u"/"_s + name;

        if (!QFile::rename(source, destination)) {
            emit error(u"Move failed: %1 to %2"_s.arg(info.fileName(), name));
            continue;
        }

        m_index.add(name, hash);

        // Written after every move, not at stop: this runs unattended for
        // hours, and a crash would otherwise lose the whole session's hashes
        // and re-import everything next time.
        m_index.save(m_collectionDir);

        ++m_collected;
        emit imageMoved(source, destination);
        emit status(m_collected, m_skipped);
    }
}

} // namespace tc
