#include <app/logger.h>
#include <QDateTime>
#include <QFile>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

constexpr qsizetype kHistoryCap = 5000;

} // namespace

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

void Logger::log(const QString& message)
{
    const QString entry =
        QDateTime::currentDateTime().toString(u"[hh:mm:ss]"_s) + u":  "_s + message;

    m_history << entry;
    if (m_history.size() > kHistoryCap) m_history.removeFirst();

    // Read once: unset costs one empty check per call.
    static const QString sinkPath = qEnvironmentVariable("TAGCOMPOSER_LOG_FILE");
    if (!sinkPath.isEmpty()) {
        QFile file(sinkPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
            file.write(entry.toUtf8() + '\n');
    }

    emit messageLogged(entry);
}

const QStringList& Logger::history() const
{
    return m_history;
}

} // namespace tc
