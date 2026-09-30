#pragma once
#include <core/composer_doc.h>
#include <core/prompt.h>
#include <QWidget>

class QVBoxLayout;

namespace tc {

// Draws bucketed pipeline output and emits edits. Holds no document state.
class TagListView : public QWidget {
    Q_OBJECT

public:
    explicit TagListView(QWidget* parent = nullptr);

    void setBuckets(const QList<TagBucket>& buckets, const ComposerDoc& doc);

signals:
    // Keyed by the activeTags key, not the displayed text.
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
