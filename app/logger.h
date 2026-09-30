#pragma once
#include <QObject>
#include <QString>
#include <QStringList>

namespace tc {

// Capped in-memory log shown on the Settings page. Set TAGCOMPOSER_LOG_FILE
// to also append to a file.
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
