#pragma once
#include <tagger/tagger_model.h>
#include <QObject>
#include <QString>
#include <QStringList>
#include <atomic>

namespace tc {

// Runs a TaggerModel over a folder of images on a background thread, writing
// `<outputRoot>/<rel>/foo.txt` beside each `<inputRoot>/<rel>/foo.png`. An
// existing .txt is overwritten.
//
// cancel() sets a flag that is read between images. Stopping mid-inference
// would need ONNX Runtime's RunOptions::SetTerminate, which is not worth the
// plumbing when a single image takes under a couple of seconds.
class BatchTagger : public QObject {
    Q_OBJECT

public:
    explicit BatchTagger(QObject* parent = nullptr);
    ~BatchTagger() override;

    bool isRunning() const;

    // Ignored while a run is in flight. moveImages also relocates each source
    // image next to its .txt, and does nothing when the two roots are the
    // same. cooldownMs throttles between images.
    void start(TaggerModel* model, const QString& inputRoot, const QString& outputRoot,
               float threshold, bool recursive, bool moveImages, int cooldownMs);

    void cancel();

signals:
    void scanned(int total); // once, after discovery and before any inference
    void imageTagged(const QString& relativePath, const tc::TaggerResult& result);
    void imageFailed(const QString& relativePath, const QString& reason);
    void progress(int done, int total);
    void finished(bool cancelled);

private:
    static QStringList discoverImages(const QString& root, bool recursive);

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_cancel{false};
};

} // namespace tc
