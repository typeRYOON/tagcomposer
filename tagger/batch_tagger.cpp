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

// The cooldown is slept in slices rather than one call, so cancelling during a
// long one is felt within a frame instead of after it.
constexpr int kSleepSliceMs = 50;

} // namespace

BatchTagger::BatchTagger(QObject* parent) : QObject(parent)
{
    // Registered before the worker exists: a queued connection carrying an
    // unregistered type drops the signal at runtime with only a warning.
    qRegisterMetaType<TaggerResult>("tc::TaggerResult");
    qRegisterMetaType<TaggerPrediction>("tc::TaggerPrediction");
}

BatchTagger::~BatchTagger()
{
    cancel();

    // The worker captured `this`, so it has to be finished before the members
    // it touches go away.
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

    // Compared clean, or a trailing slash on one of them reads as a different
    // root and the move below starts shuffling files onto themselves.
    const bool sameRoot = QDir::cleanPath(inputRoot) == QDir::cleanPath(outputRoot);
    const int cooldown = std::max(0, cooldownMs);

    // The model outlives any run: the library owns it.
    //
    // The QFuture is dropped on purpose. The pool owns the task either way,
    // and the only thing anyone waits on is m_running, which the task clears
    // on its way out.
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

            // The output mirrors the input's shape, so a recursive run keeps
            // its subdirectories rather than flattening them into one folder.
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

    // Sorted so a resumed or re-run batch walks the folder the same way twice.
    std::sort(images.begin(), images.end());
    return images;
}

} // namespace tc
