#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include <QHash>

class QImage;

namespace core {

struct ImageEdits;

struct WorkflowInput {
    QString uuid;
    QString displayName;
    QString originalFile;
    int width = 0;
    int height = 0;
};

// Source of truth for image-typed workflow variables. Files live under
// <cacheDir>/<uuid>.png; ComfyUI's input/ is a write-through mirror.
class WorkflowInputCache : public QObject {
    Q_OBJECT
public:
    explicit WorkflowInputCache(const QString& cacheDir, QObject* parent = nullptr);

    // Re-encodes to PNG with alpha. Returns the uuid or empty on failure.
    QString importFromFile(const QString& srcPath);

    void remove(const QString& uuid);
    bool has(const QString& uuid) const;

    QString cacheDir() const
    {
        return m_cacheDir;
    }
    QString localPath(const QString& uuid) const;

    // Returns localPath when edits.enabled is false, otherwise renders and
    // caches a variant under _edited/<editsHash>/<uuid>.png. Basename stays
    // <uuid>.png so ComfyUI's path stays `tagcomposer/<uuid>.png`.
    QString resolveEdited(const QString& uuid, const ImageEdits& edits) const;

    // Each saveMask writes a fresh uuid; dedupe is delegated to editsHash.
    QString saveMask(const QImage& mask);
    QImage loadMask(const QString& maskId) const;
    void removeMask(const QString& maskId);
    QString maskPath(const QString& maskId) const;

    // Subfolder under ComfyUI's input/ - keeps us out of clipspace/.
    static QString serverSubfolder()
    {
        return "tagcomposer";
    }
    QString serverFilename(const QString& uuid) const;

    QList<WorkflowInput> all() const;
    WorkflowInput get(const QString& uuid) const;

signals:
    void added(QString uuid);
    void removed(QString uuid);

private:
    void readIndex();
    void writeIndex() const;

    QString m_cacheDir;
    QHash<QString, WorkflowInput> m_byUuid;
};

} // namespace core
