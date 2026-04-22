#include <gui/appmainwindow.h>
#include <gui/entryview.h>
#include <utils/qutils.h>
#include <utils/appconfig.h>
#include <QApplication>
#include <QTimer>
#include <QVBoxLayout>
#include <QLabel>

#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>


using namespace utils;
using namespace model;

namespace gui {


AppMainWindow::AppMainWindow(QWidget* parent)
    : QMainWindow{ parent },
    m_entryModel{ new EntryModel(this) }
{
    setWindowTitle("Viewer");
    setWindowOpacity(0.0);

    QWidget* root = new QWidget;
    QVBoxLayout* mainLayout = new QVBoxLayout;

    QLineEdit* search = new QLineEdit;
    search->setPlaceholderText("search tags...");

    EntryView* ev = new EntryView(m_entryModel);
    ev->setStyleSheet("Border: 0px");

    // BOTTOM PANEL
    //QHBoxLayout* bottom = new QHBoxLayout;

    /*QLabel* detailImage = new QLabel;
    detailImage->setFixedSize(180, 231);*/


    //QVBoxLayout* detailText = new QVBoxLayout;
    //QLineEdit* detailTitle  = new QLineEdit;
    //QTextEdit* detailTags   = new QTextEdit;

    //QPushButton* copyBtn = new QPushButton("Copy Tags");

    //detailText->addWidget(detailTitle);
    //detailText->addWidget(detailTags);
    //detailText->addWidget(copyBtn);

    /*bottom->addWidget(detailImage);*/
    //bottom->addLayout(detailText);

    // SIDEBAR
    QVBoxLayout* sidebar = new QVBoxLayout;
    QLineEdit* titleInput = new QLineEdit;
    titleInput->setPlaceholderText("Title");

    QTextEdit* tagsInput = new QTextEdit;
    tagsInput->setPlaceholderText("tags (comma separated)");
    tagsInput->setMinimumHeight(80);

    //DropLabel* drop = new DropLabel;

    QPushButton* addBtn = new QPushButton("Add Entry");

    sidebar->addWidget(titleInput);
    sidebar->addWidget(tagsInput);
    //sidebar->addWidget(drop);
    sidebar->addWidget(addBtn);
    sidebar->addStretch();

    QWidget* sidebarWidget = new QWidget;
    sidebarWidget->setLayout(sidebar);
    sidebarWidget->setFixedWidth(250);

    QHBoxLayout* middle = new QHBoxLayout;
    middle->addWidget(ev, 3);
    middle->addWidget(sidebarWidget, 1);

    mainLayout->addWidget(search);
    mainLayout->addLayout(middle);
    //mainLayout->addLayout(bottom);

    root->setLayout(mainLayout);
    setCentralWidget(root);
    setStyleSheet("background: black; color: white;");


    QTimer* debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(150);

    connect(search, &QLineEdit::textChanged, debounce, qOverload<>(&QTimer::start));
    connect(debounce, &QTimer::timeout, this, [this, search, ev]() {
        ev->query(search->text());
    });

    QTimer::singleShot(500, this, [ev, this]() {
        showMaximized();
        propertyAnimate(this, "windowOpacity", 0.0, 1.0, 500, QEasingCurve::InOutSine);
    });

    ev->query("");
}


void AppMainWindow::closeEvent(QCloseEvent* event)
{
    static bool isClosing{ false };
    if (isClosing) {
        event->accept();
        return;
    }
    event->ignore();
    qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
    isClosing = true;

    connect(
        propertyAnimate(this, "windowOpacity", 1.0, 0.0, 500, QEasingCurve::InOutSine),
        &QPropertyAnimation::finished,
        this,
        []() { qApp->quit(); }
    );
}

}



