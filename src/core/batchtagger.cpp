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
const QStringList kImageFilters = {
    "*.png", "*.jpg", "*.jpeg", "*.webp", "*.bmp", "*.gif"
};
}

BatchTagger::BatchTagger(QObject* parent) : QObject(parent)
{
    // Queued-connection signal payloads need to round-trip through QMetaType.
    // Q_DECLARE_METATYPE in the header opts the type in; this call ensures
    // the registry is populated before the worker thread emits.
    qRegisterMetaType<TagResult>("core::TagResult");
    qRegisterMetaType<TagPrediction>("core::TagPrediction");
}

BatchTagger::~BatchTagger()
{
    cancel();
    // Wait for the worker to wind down before tearing the object down - the
    // worker holds a `this` pointer for the lifetime of its loop.
    while (m_running.load()) QThread::msleep(10);
}

void BatchTagger::start(AutoTaggerModel* model,
                        const QString&   inputRoot,
                        const QString&   outputRoot,
                        float            threshold,
                        bool             recursive,
                        bool             moveImages,
                        int              cooldownMs)
{
    if (m_running.load() || !model) return;

    m_running.store(true);
    m_cancel .store(false);

    // Same canonical path = no-op for the move (the source already lives
    // exactly where it would land). Compared via QDir::cleanPath to avoid
    // trailing-slash mismatches.
    const bool sameRoot =
        QDir::cleanPath(inputRoot) == QDir::cleanPath(outputRoot);

    const int cooldown = qMax(0, cooldownMs);

    // Capture by value so the worker has stable copies of every input. The
    // model* is owned by the library - outlives any single batch run.
    QtConcurrent::run([this, model, inputRoot, outputRoot,
                       threshold, recursive, moveImages, sameRoot, cooldown]() {
        const QStringList images = discoverImages(inputRoot, recursive);
        emit scanned(images.size());

        QDir inDir(inputRoot);
        bool cancelled = false;
        int  done      = 0;

        for (const QString& abs : images) {
            if (m_cancel.load()) { cancelled = true; break; }

            const QString rel = inDir.relativeFilePath(abs);
            TagResult res = model->tag(abs, threshold);

            if (res.tags.isEmpty() && res.rating.isEmpty()) {
                // Either decode failed or the model returned literally
                // nothing above threshold + zero ratings. The latter is
                // weird; surface as a failure so the user can see it.
                emit imageFailed(rel, "no tags produced");
                ++done;
                emit progress(done, images.size());
                continue;
            }

            // <outputRoot>/<rel>/foo.png → <outputRoot>/<rel>/foo.txt
            // (and optionally also moves the image to <outputRoot>/<rel>/foo.png).
            const QFileInfo relInfo(rel);
            const QString   relDir = relInfo.path();  // "." for top-level
            const QString   outDir = (relDir.isEmpty() || relDir == ".")
                ? outputRoot
                : outputRoot + "/" + relDir;

            // Make sure the mirrored subtree exists - overwrite policy means
            // we don't probe for the .txt's existence first.
            QDir().mkpath(outDir);

            const QString txtAbs = outDir + "/" + relInfo.completeBaseName() + ".txt";

            QFile f(txtAbs);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                QTextStream ts(&f);
                QStringList parts;
                for (const TagPrediction& tp : res.tags) parts << tp.tag;
                ts << parts.join(", ");
            }

            // Move the source image alongside its .txt. Skip when in==out
            // (already there) or when the source/dest paths resolve to the
            // same file. Overwrites any existing destination - matches the
            // .txt overwrite policy.
            if (moveImages && !sameRoot) {
                const QString destAbs = outDir + "/" + relInfo.fileName();
                if (QFileInfo(abs).canonicalFilePath()
                    != QFileInfo(destAbs).canonicalFilePath())
                {
                    if (QFile::exists(destAbs)) QFile::remove(destAbs);
                    QFile::rename(abs, destAbs);
                }
            }

            emit imageTagged(rel, res);
            ++done;
            emit progress(done, images.size());

            // Cooldown - give the CPU a breather between inferences. Chunked
            // so a cancel during a long cooldown still feels responsive.
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

    QDirIterator::IteratorFlags flags = recursive
        ? QDirIterator::Subdirectories
        : QDirIterator::NoIteratorFlags;

    QDirIterator it(root, kImageFilters, QDir::Files, flags);
    while (it.hasNext()) out << it.next();

    std::sort(out.begin(), out.end());
    return out;
}

} // namespace core
