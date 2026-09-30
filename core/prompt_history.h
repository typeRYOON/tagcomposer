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
    QString workflowName;
    QString positivePrompt; // post-pipeline, as sent

    // Replayed verbatim on re-queue, so the seed stays the same.
    QString renderedJson;

    // Composer and workflow state at queue time.
    SavedState snapshot;

    QList<Lora> lorasUsed;
    QString batchEntryUuid; // empty for a composer run
};

// Session-only log of queued prompts, newest first.
class PromptHistory : public QObject {
    Q_OBJECT

public:
    explicit PromptHistory(QObject* parent = nullptr);

    const QList<PromptRecord>& records() const;

    void append(PromptRecord record);
    void clear();

signals:
    void recordAdded(int index); // always 0; newest first
    void cleared();

private:
    QList<PromptRecord> m_records;
};

} // namespace tc
