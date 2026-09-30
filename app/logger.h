#pragma once
#include <QObject>
#include <QString>
#include <QStringList>

namespace tc {

// In-memory log with a capped history, surfaced by the Settings page's LOG
// section. A GUI app has no console, so TAGCOMPOSER_LOG_FILE adds a file sink
// for anything that has to be readable from a script.
class Logger : public QObject {
    Q_OBJECT

public:
    static Logger& instance();

    void log(const QString& message);
    const QStringList& history() const;

signals:
    void messageLogged(const QString& formatted);

private:
    Logger() = default;

    QStringList m_history;
};

} // namespace tc
