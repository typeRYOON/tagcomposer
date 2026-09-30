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

// Files behind image workflow variables, stored as <cacheDir>/<uuid>.png.
// In app/ rather than core/ because it needs QImage.
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

    // localPath, or the rendered edit variant at _edited/<hash>/<uuid>.png. The
    // basename stays <uuid> so the server-side name doesn't change.
    QString resolveEdited(const QString& uuid, const ImageEdits& edits) const;

    // Each save gets a fresh id.
    QString saveMask(const QImage& mask);
    QImage loadMask(const QString& maskId) const;
    void removeMask(const QString& maskId);
    QString maskPath(const QString& maskId) const;

    // Subfolder under ComfyUI's input/.
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
