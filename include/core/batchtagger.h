#pragma once
#include <core/autotaggermodel.h>
#include <QObject>
#include <QString>
#include <QStringList>
#include <atomic>

namespace core {

// Drives an AutoTaggerModel over a folder of images on a background thread.
//
// For each `<inputRoot>/<rel>/foo.png`, writes a comma-joined tag list to
// `<outputRoot>/<rel>/foo.txt`. Existing .txts are overwritten.
//
// cancel() flips an atomic checked between images; mid-inference cancel
// would need ORT's RunOptions.SetTerminate, skipped since one image is < ~2s.
class BatchTagger : public QObject {
    Q_OBJECT
public:
    explicit BatchTagger(QObject* parent = nullptr);
    ~BatchTagger() override;

    bool isRunning() const
    {
        return m_running.load();
    }

    // No-op if already running. moveImages also relocates each source image
    // alongside its .txt (no-op when inputRoot == outputRoot). cooldownMs is
    // a between-image throttle, chunked so cancel stays responsive.
    void start(AutoTaggerModel* model, const QString& inputRoot, const QString& outputRoot,
               float threshold, bool recursive, bool moveImages, int cooldownMs);

    void cancel();

signals:
    void scanned(int total); // fires once after discovery, before inference
    // relPath is relative to inputRoot.
    void imageTagged(QString relPath, core::TagResult result);
    void imageFailed(QString relPath, QString reason);
    void progress(int done, int total);
    void finished(bool cancelled);

private:
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_cancel{false};

    static QStringList discoverImages(const QString& root, bool recursive);
};

} // namespace core
