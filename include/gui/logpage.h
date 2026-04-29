#pragma once
#include <QWidget>
#include <QPlainTextEdit>

namespace gui {

class LogPage : public QWidget {
    Q_OBJECT
public:
    explicit LogPage(QWidget* parent = nullptr);

public slots:
    void appendMessage(const QString& message);

private:
    QPlainTextEdit* m_log;
};

} // namespace gui
