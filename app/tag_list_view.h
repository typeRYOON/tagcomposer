#pragma once
#include <core/composer_doc.h>
#include <core/prompt.h>
#include <QWidget>

class QVBoxLayout;

namespace tc {

// Draws bucketed pipeline output. Holds no document state of its own: it is
// handed buckets plus the document they came from, and emits what the user
// asked for. The window decides what that means.
//
// The document is only read to tell an editable tag from a rule-injected one,
// which is not in activeTags and so has nothing to weight or remove.
class TagListView : public QWidget {
    Q_OBJECT

public:
    explicit TagListView(QWidget* parent = nullptr);

    void setBuckets(const QList<TagBucket>& buckets, const ComposerDoc& doc);

signals:
    // All three carry the activeTags key, not the displayed text, so a tag
    // containing a variable addresses the right entry.
    void removeRequested(const QString& key);
    void weightChanged(const QString& key, double weight);
    void renameRequested(const QString& key, const QString& to);

private:
    void clearRows();
    void addHeader(const QString& text);
    void addRow(const PipelineTag& tag, bool editable);

    QVBoxLayout* m_rows = nullptr;
};

} // namespace tc
