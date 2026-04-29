#include <gui/homepage.h>
#include <QVBoxLayout>
#include <QLabel>

namespace gui {

HomePage::HomePage(QWidget* parent) : QWidget(parent)
{
    setObjectName("HomePage");
    setAttribute(Qt::WA_StyledBackground, true);

    QPixmap logo(":/img/ryoon_logo.png");
    QLabel* ryoon_logo = new QLabel();
    ryoon_logo->setPixmap(
        logo.scaled(
            50, 50, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation
        )
    );

    QVBoxLayout* layout = new QVBoxLayout(this);
    QHBoxLayout* logoRow = new QHBoxLayout;

    logoRow->addStretch();
    logoRow->addWidget(ryoon_logo);
    logoRow->addSpacing(20);

    layout->addStretch();
    layout->addLayout(logoRow);
    layout->addSpacing(10);
}

} // namespace gui
