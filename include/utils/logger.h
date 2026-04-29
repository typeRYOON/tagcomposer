#pragma once
#include <QObject>
#include <QList>
#include <QString>

namespace utils {

class Logger : public QObject {
    Q_OBJECT
public:
    static Logger& instance();

    void log(const QString& message);
    const QList<QString>& history() const;

signals:
    void messageLogged(const QString& formatted);

private:
    Logger() = default;
    QList<QString> m_history;
};

} // namespace utils
