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
    const QString entry = QDateTime::currentDateTime().toString("[hh:mm:ss]") + ":  " + message;
    m_history << entry;
    emit messageLogged(entry);
}

const QList<QString>& Logger::history() const
{
    return m_history;
}

} // namespace utils
