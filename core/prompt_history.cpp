#include <core/prompt_history.h>

namespace tc {

PromptHistory::PromptHistory(QObject* parent) : QObject(parent) {}

const QList<PromptRecord>& PromptHistory::records() const
{
    return m_records;
}

void PromptHistory::append(PromptRecord record)
{
    m_records.prepend(std::move(record));
    emit recordAdded(0);
}

void PromptHistory::clear()
{
    if (m_records.isEmpty()) return;
    m_records.clear();
    emit cleared();
}

} // namespace tc
