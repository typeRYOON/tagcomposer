#pragma once
#include <core/entry.h>
#include <core/saved_state.h>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

namespace tc {

struct PromptRecord {
    QDateTime queuedAt;
    QString workflowName;   // the label the list row shows
    QString positivePrompt; // the post-pipeline string actually sent

    // The final JSON. A re-queue replays this verbatim, which is what keeps
    // the seed identical rather than rolling a new one.
    QString renderedJson;

    // The composer and workflow state at queue time, which is what
    // save-as-state and restore-to-composer work from.
    SavedState snapshot;

    QList<Lora> lorasUsed; // the stack as ComfyUI received it
    QString batchEntryUuid; // empty for a composer run
};

// A session-only log of everything pushed to ComfyUI. The shell appends one
// record per queue so the history page can inspect, replay or restore each
// push without polling anything. Newest first.
class PromptHistory : public QObject {
    Q_OBJECT

public:
    explicit PromptHistory(QObject* parent = nullptr);

    const QList<PromptRecord>& records() const;

    void append(PromptRecord record);
    void clear();

signals:
    void recordAdded(int index); // always 0, since records are prepended
    void cleared();

private:
    QList<PromptRecord> m_records;
};

} // namespace tc
