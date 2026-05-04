#pragma once
#include <core/autotaggermodel.h>
#include <QObject>
#include <QString>
#include <QStringList>
#include <atomic>

namespace core {

// Drives an AutoTaggerModel over a folder of images. Runs on a background
// thread; emits progress + per-image signals back to the GUI thread via
// queued connections (Qt::AutoConnection from a worker thread does this for
// free).
//
// Output policy: for each tagged image at `<inputRoot>/<rel>/foo.png`, writes
// `<outputRoot>/<rel>/foo.txt` containing the comma-separated tags above
// `threshold`. Existing .txt files at the same path are overwritten without
// prompt - this is a regenerate-from-source pipeline.
//
// Cancellation: cancel() flips an atomic; the worker checks it between images
// (mid-inference cancel would require ORT RunOptions.SetTerminate per call,
// which we skip for simplicity since one image is < ~2s).
class BatchTagger : public QObject {
    Q_OBJECT
public:
    explicit BatchTagger(QObject* parent = nullptr);
    ~BatchTagger() override;

    bool isRunning() const { return m_running.load(); }

    // Kicks off a batch. No-op if already running. `moveImages` moves each
    // tagged source image to its mirrored location under `outputRoot` after
    // its .txt is written; ignored when `inputRoot == outputRoot` since the
    // source already sits where it would land. `cooldownMs` is the throttle
    // applied after each image's inference (chunked into 50 ms slices so
    // cancel still responds promptly on long cooldowns).
    void start(AutoTaggerModel* model,
               const QString&   inputRoot,
               const QString&   outputRoot,
               float            threshold,
               bool             recursive,
               bool             moveImages,
               int              cooldownMs);

    void cancel();

signals:
    // Emitted once after image discovery, before any inference runs.
    void scanned(int total);

    // One per image, regardless of whether the .txt was written. `relPath`
    // is the input path relative to inputRoot (mirrors how the .txt is
    // placed under outputRoot).
    void imageTagged(QString relPath, core::TagResult result);
    void imageFailed(QString relPath, QString reason);

    void progress(int done, int total);
    void finished(bool cancelled);

private:
    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_cancel { false };

    static QStringList discoverImages(const QString& root, bool recursive);
};

} // namespace core
