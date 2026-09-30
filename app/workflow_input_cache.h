#pragma once
#include <core/workflow.h>
#include <QHash>
#include <QObject>
#include <QString>

class QImage;

namespace tc {

struct WorkflowInput {
    QString uuid;
    QString displayName;
    QString originalFile;
    int width = 0;
    int height = 0;
};

// Owns the files behind image-typed workflow variables. They live at
// <cacheDir>/<uuid>.png; ComfyUI's input/ is a write-through mirror.
//
// This is app-side rather than core because it decodes and renders images,
// and tc_core is Qt6::Core only.
class WorkflowInputCache : public QObject {
    Q_OBJECT

public:
    explicit WorkflowInputCache(const QString& cacheDir, QObject* parent = nullptr);

    // Re-encodes to PNG with alpha. Empty on failure.
    QString importFromFile(const QString& sourcePath);

    void remove(const QString& uuid);
    bool has(const QString& uuid) const;

    const QString& cacheDir() const;
    QString localPath(const QString& uuid) const;

    // localPath when the edits are off; otherwise renders the variant into
    // _edited/<hash>/<uuid>.png and returns that. The basename stays <uuid>
    // so the server-side path does not change with the variant.
    QString resolveEdited(const QString& uuid, const ImageEdits& edits) const;

    // Every save mints a fresh id; deduplication is the edits hash's job.
    QString saveMask(const QImage& mask);
    QImage loadMask(const QString& maskId) const;
    void removeMask(const QString& maskId);
    QString maskPath(const QString& maskId) const;

    // A subfolder under ComfyUI's input/, which keeps us out of clipspace/.
    static QString serverSubfolder();
    QString serverFilename(const QString& uuid) const;

    QList<WorkflowInput> all() const;
    WorkflowInput get(const QString& uuid) const;

signals:
    void added(const QString& uuid);
    void removed(const QString& uuid);

private:
    void readIndex();
    void writeIndex() const;

    QString m_cacheDir;
    QHash<QString, WorkflowInput> m_byUuid;
};

} // namespace tc
