#include <utils/logger.h>
#include <QDateTime>

namespace utils {

Logger& Logger::instance()
{
    static Logger inst;
    return inst;
}

void Logger::log(const QString& message)
{
    constexpr int kHistoryCap = 5000;
    const QString entry = QDateTime::currentDateTime().toString("[hh:mm:ss]") + ":  " + message;
    m_history << entry;
    if (m_history.size() > kHistoryCap) m_history.removeFirst();
    emit messageLogged(entry);
}

const QList<QString>& Logger::history() const
{
    return m_history;
}

} // namespace utils
