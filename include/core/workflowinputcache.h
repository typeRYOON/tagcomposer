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
    QString displayName;     // shown in UI (defaults to source filename)
    QString originalFile;    // source filename at import time, for traceability
    int     width  = 0;
    int     height = 0;
};

// Owns data/workflow_inputs/. Files live as <uuid>.png; metadata in _index.json.
// Source of truth for image-typed workflow variables; ComfyUI's input folder is
// a write-through mirror.
class WorkflowInputCache : public QObject {
    Q_OBJECT
public:
    explicit WorkflowInputCache(const QString& cacheDir, QObject* parent = nullptr);

    // Copies srcPath into the cache (re-encoded as PNG with alpha when not
    // already a PNG). Returns the new uuid, or empty string on failure.
    QString importFromFile(const QString& srcPath);

    void    remove(const QString& uuid);
    bool    has(const QString& uuid) const;

    QString cacheDir()    const { return m_cacheDir; }
    QString localPath(const QString& uuid) const;        // <cacheDir>/<uuid>.png

    // Returns the path to feed to upload. If edits.enabled is false, this is
    // just localPath(uuid). Otherwise, renders the edited variant under
    // <cacheDir>/_edited/<editsHash>/<uuid>.png (cached on disk; subsequent
    // calls with the same edits hit the cache). Basename stays <uuid>.png so
    // ComfyUI sees the file at the expected `tagcomposer/<uuid>.png`.
    QString resolveEdited(const QString& uuid, const ImageEdits& edits) const;

    // Mask storage for the clip editor. saveMask writes the QImage to
    // <cacheDir>/_masks/<newUuid>.png and returns the new uuid. Each save
    // produces a fresh uuid (we don't try to dedupe identical masks - the
    // editsHash will differ anyway because maskId is content-addressed).
    QString saveMask(const QImage& mask);
    QImage  loadMask(const QString& maskId) const;
    void    removeMask(const QString& maskId);
    QString maskPath(const QString& maskId) const;

    // The relative path string substituted into a workflow's LoadImage.image
    // field. The leading subfolder keeps us out of ComfyUI's clipspace/ namespace.
    static QString serverSubfolder()                     { return "tagcomposer"; }
    QString serverFilename(const QString& uuid) const;   // tagcomposer/<uuid>.png

    QList<WorkflowInput> all() const;
    WorkflowInput        get(const QString& uuid) const;

signals:
    void added(QString uuid);
    void removed(QString uuid);

private:
    void readIndex();
    void writeIndex() const;

    QString                  m_cacheDir;
    QHash<QString, WorkflowInput> m_byUuid;
};

} // namespace core
