#include <core/batchtagger.h>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QThread>
#include <QtConcurrent>

namespace core {

namespace {
const QStringList kImageFilters = {"*.png", "*.jpg", "*.jpeg", "*.webp", "*.bmp", "*.gif"};
}

BatchTagger::BatchTagger(QObject* parent) : QObject(parent)
{
    // Populate the metatype registry before the worker thread can emit.
    qRegisterMetaType<TagResult>("core::TagResult");
    qRegisterMetaType<TagPrediction>("core::TagPrediction");
}

BatchTagger::~BatchTagger()
{
    cancel();
    // Worker holds `this`; wait for it to wind down before destruction.
    while (m_running.load())
        QThread::msleep(10);
}

void BatchTagger::start(AutoTaggerModel* model, const QString& inputRoot, const QString& outputRoot,
                        float threshold, bool recursive, bool moveImages, int cooldownMs)
{
    if (m_running.load() || !model) return;

    m_running.store(true);
    m_cancel.store(false);

    // cleanPath comparison sidesteps trailing-slash mismatches.
    const bool sameRoot = QDir::cleanPath(inputRoot) == QDir::cleanPath(outputRoot);

    const int cooldown = qMax(0, cooldownMs);

    // model* outlives any batch (owned by the library).
    QtConcurrent::run([this, model, inputRoot, outputRoot, threshold, recursive, moveImages,
                       sameRoot, cooldown]() {
        const QStringList images = discoverImages(inputRoot, recursive);
        emit scanned(images.size());

        QDir inDir(inputRoot);
        bool cancelled = false;
        int done = 0;

        for (const QString& abs : images) {
            if (m_cancel.load()) {
                cancelled = true;
                break;
            }

            const QString rel = inDir.relativeFilePath(abs);
            TagResult res = model->tag(abs, threshold);

            if (res.tags.isEmpty() && res.rating.isEmpty()) {
                emit imageFailed(rel, "no tags produced");
                ++done;
                emit progress(done, images.size());
                continue;
            }

            const QFileInfo relInfo(rel);
            const QString relDir = relInfo.path();
            const QString outDir =
                (relDir.isEmpty() || relDir == ".") ? outputRoot : outputRoot + "/" + relDir;
            QDir().mkpath(outDir);

            const QString txtAbs = outDir + "/" + relInfo.completeBaseName() + ".txt";

            QFile f(txtAbs);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                QTextStream ts(&f);
                QStringList parts;
                for (const TagPrediction& tp : res.tags)
                    parts << tp.tag;
                ts << parts.join(", ");
            }

            if (moveImages && !sameRoot) {
                const QString destAbs = outDir + "/" + relInfo.fileName();
                if (QFileInfo(abs).canonicalFilePath() != QFileInfo(destAbs).canonicalFilePath()) {
                    if (QFile::exists(destAbs)) QFile::remove(destAbs);
                    QFile::rename(abs, destAbs);
                }
            }

            emit imageTagged(rel, res);
            ++done;
            emit progress(done, images.size());

            // Chunked so cancel during a long cooldown feels responsive.
            int remaining = cooldown;
            while (remaining > 0 && !m_cancel.load()) {
                const int slice = qMin(remaining, 50);
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
    QStringList out;
    if (root.isEmpty() || !QDir(root).exists()) return out;

    QDirIterator::IteratorFlags flags =
        recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;

    QDirIterator it(root, kImageFilters, QDir::Files, flags);
    while (it.hasNext())
        out << it.next();

    std::sort(out.begin(), out.end());
    return out;
}

} // namespace core
