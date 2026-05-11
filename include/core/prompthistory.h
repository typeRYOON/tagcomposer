#pragma once
#include <core/entry.h>
#include <core/savedstate.h>
#include <QObject>
#include <QDateTime>
#include <QList>
#include <QString>

namespace core {

struct PromptRecord {
    QDateTime queuedAt;
    QString workflowName;     // display label for the list row
    QString positivePrompt;   // post-pipeline string actually sent
    QString renderedJson;     // final JSON sent to ComfyUI - re-queue replays
                              // this verbatim so the seed stays identical
    SavedState snapshot;      // composer/workflow state at queue time;
                              // drives save-as-state and restore-to-composer
    QList<LoraConfig> lorasUsed; // post-heal stack (what ComfyUI got)
    int batchEntryId = -1;    // -1 = composer Run; entry id for batch runs
};

// Session-only collector of prompts pushed to ComfyUI. AppMainWindow appends
// one record per queuePrompt call so the history page can inspect, replay,
// or restore each push without polling. Newest-first storage.
class PromptHistory : public QObject {
    Q_OBJECT
public:
    explicit PromptHistory(QObject* parent = nullptr);

    const QList<PromptRecord>& records() const
    {
        return m_records;
    }

    void append(PromptRecord rec);
    void clear();

signals:
    void recordAdded(int index); // freshly inserted at this index (always 0)
    void cleared();

private:
    QList<PromptRecord> m_records;
};

} // namespace core
