#include <utils/logger.h>
#include <QDateTime>
#include <QFile>

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

    // Opt-in file sink. The Settings log view is the normal surface, but
    // startup timings need to be readable from a script too - a GUI app has
    // no console to log to. Unset (the default) costs one empty-string check.
    static const QString sinkPath = qEnvironmentVariable("TAGCOMPOSER_LOG_FILE");
    if (!sinkPath.isEmpty()) {
        QFile f(sinkPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
            f.write(entry.toUtf8() + '\n');
    }

    emit messageLogged(entry);
}

const QList<QString>& Logger::history() const
{
    return m_history;
}

} // namespace utils
