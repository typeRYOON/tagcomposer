#include <tagger/batch_tagger.h>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QtConcurrent>
#include <algorithm>
#include <tuple>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

const QStringList kImageFilters = {u"*.png"_s, u"*.jpg"_s,  u"*.jpeg"_s,
                                   u"*.webp"_s, u"*.bmp"_s, u"*.gif"_s};

// Sleep in slices so cancel stays responsive.
constexpr int kSleepSliceMs = 50;

} // namespace

BatchTagger::BatchTagger(QObject* parent) : QObject(parent)
{
    // Needed for queued connections from the worker.
    qRegisterMetaType<TaggerResult>("tc::TaggerResult");
    qRegisterMetaType<TaggerPrediction>("tc::TaggerPrediction");
}

BatchTagger::~BatchTagger()
{
    cancel();

    // The worker captures this.
    while (m_running.load()) QThread::msleep(10);
}

bool BatchTagger::isRunning() const
{
    return m_running.load();
}

void BatchTagger::start(TaggerModel* model, const QString& inputRoot, const QString& outputRoot,
                        float threshold, bool recursive, bool moveImages, int cooldownMs)
{
    if (m_running.load() || !model) return;

    m_running.store(true);
    m_cancel.store(false);

    // Compare cleaned paths so a trailing slash doesn't count as a different root.
    const bool sameRoot = QDir::cleanPath(inputRoot) == QDir::cleanPath(outputRoot);
    const int cooldown = std::max(0, cooldownMs);

    // The library owns the model. The future is dropped; m_running tracks the task.
    std::ignore =
        QtConcurrent::run([this, model, inputRoot, outputRoot, threshold, recursive, moveImages,
                           sameRoot, cooldown]() {
        const QStringList images = discoverImages(inputRoot, recursive);
        emit scanned(int(images.size()));

        const QDir inputDir(inputRoot);
        bool cancelled = false;
        int done = 0;

        for (const QString& absolute : images) {
            if (m_cancel.load()) {
                cancelled = true;
                break;
            }

            const QString relative = inputDir.relativeFilePath(absolute);
            const TaggerResult result = model->tag(absolute, threshold);

            if (result.tags.isEmpty() && result.rating.isEmpty()) {
                emit imageFailed(relative, u"no tags produced"_s);
                emit progress(++done, int(images.size()));
                continue;
            }

            // Mirror the input's subdirectories.
            const QFileInfo info(relative);
            const QString subdirectory = info.path();
            const QString directory = subdirectory.isEmpty() || subdirectory == u"."_s
                                          ? outputRoot
                                          : outputRoot + u"/"_s + subdirectory;
            QDir().mkpath(directory);

            QFile file(directory + u"/"_s + info.completeBaseName() + u".txt"_s);
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                QStringList tags;
                tags.reserve(result.tags.size());
                for (const TaggerPrediction& prediction : result.tags) tags << prediction.tag;
                file.write(tags.join(u", "_s).toUtf8());
            }

            if (moveImages && !sameRoot) {
                const QString destination = directory + u"/"_s + info.fileName();
                if (QFileInfo(absolute).canonicalFilePath()
                    != QFileInfo(destination).canonicalFilePath()) {
                    if (QFile::exists(destination)) QFile::remove(destination);
                    QFile::rename(absolute, destination);
                }
            }

            emit imageTagged(relative, result);
            emit progress(++done, int(images.size()));

            for (int remaining = cooldown; remaining > 0 && !m_cancel.load();) {
                const int slice = std::min(remaining, kSleepSliceMs);
                QThread::msleep(slice);
                remaining -= slice;
            }
        }

        m_running.store(false);
        emit finished(cancelled);
    });
}

void BatchTagger::cancel()
{
    if (!m_running.load()) return;
    m_cancel.store(true);
}

QStringList BatchTagger::discoverImages(const QString& root, bool recursive)
{
    if (root.isEmpty() || !QDir(root).exists()) return {};

    const QDirIterator::IteratorFlags flags =
        recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;

    QStringList images;
    QDirIterator it(root, kImageFilters, QDir::Files, flags);
    while (it.hasNext()) images << it.next();

    // Sorted for a stable order across runs.
    std::sort(images.begin(), images.end());
    return images;
}

} // namespace tc
