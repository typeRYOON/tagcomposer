#include <gui/widgets/statusbar.h>
#include <utils/logger.h>
#include <QHBoxLayout>

namespace gui {

StatusBar::StatusBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("StatusBar");
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(22);

    m_label = new QLabel(this);
    m_label->setObjectName("StatusBarLabel");
    m_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 0, 10, 0);
    layout->addWidget(m_label);

    connect(&utils::Logger::instance(), &utils::Logger::messageLogged,
            this, &StatusBar::showMessage);
}

void StatusBar::showMessage(const QString& message)
{
    QFontMetrics fm(m_label->font());
    QString elided = fm.elidedText(message, Qt::ElideRight, m_label->width());
    m_label->setText(elided);
}

} // namespace gui
