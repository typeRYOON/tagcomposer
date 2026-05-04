#include <gui/logpage.h>
#include <utils/logger.h>
#include <QVBoxLayout>

namespace gui {

LogPage::LogPage(QWidget* parent) : QWidget(parent)
{
    setObjectName("LogPage");
    setAttribute(Qt::WA_StyledBackground, true);

    m_log = new QPlainTextEdit(this);
    m_log->setObjectName("LogView");
    m_log->setReadOnly(true);
    m_log->setUndoRedoEnabled(false);
    m_log->setMaximumBlockCount(5000);

    for (const QString& msg : utils::Logger::instance().history())
        m_log->appendPlainText(msg);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_log);

    connect(&utils::Logger::instance(), &utils::Logger::messageLogged, this,
            &LogPage::appendMessage);
}

void LogPage::appendMessage(const QString& message)
{
    m_log->appendPlainText(message);
    m_log->ensureCursorVisible();
}

} // namespace gui
