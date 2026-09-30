#pragma once
#include <tagger/tagger_model.h>
#include <QObject>
#include <QString>
#include <QStringList>
#include <atomic>

namespace tc {

// Tags a folder of images on a worker thread, writing <outputRoot>/<rel>/foo.txt
// for each <inputRoot>/<rel>/foo.png (overwriting). cancel() applies between images.
class BatchTagger : public QObject {
    Q_OBJECT

public:
    explicit BatchTagger(QObject* parent = nullptr);
    ~BatchTagger() override;

    bool isRunning() const;

    // No-op while running. moveImages moves each image next to its .txt.
    void start(TaggerModel* model, const QString& inputRoot, const QString& outputRoot,
               float threshold, bool recursive, bool moveImages, int cooldownMs);

    void cancel();

signals:
    void scanned(int total); // once, before inference
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
