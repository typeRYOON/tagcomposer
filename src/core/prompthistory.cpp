#include <core/prompthistory.h>

namespace core {

PromptHistory::PromptHistory(QObject* parent) : QObject(parent) {}

void PromptHistory::append(PromptRecord rec)
{
    m_records.prepend(std::move(rec));
    emit recordAdded(0);
}

void PromptHistory::clear()
{
    if (m_records.isEmpty()) return;
    m_records.clear();
    emit cleared();
}

} // namespace core
