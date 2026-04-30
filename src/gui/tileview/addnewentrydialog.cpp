#include <gui/tileview/addnewentrydialog.h>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QUuid>
#include <QDateTime>


namespace gui {

AddEntryDialog::AddEntryDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("New Entry");
    setMinimumWidth(320);

    m_titleEdit = new QLineEdit(this);
    m_titleEdit->setPlaceholderText("Entry title");

    auto* form = new QFormLayout;
    form->addRow("Title:", m_titleEdit);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void AddEntryDialog::setFields(const QString& title)
{
    m_titleEdit->setText(title);
}

core::Entry AddEntryDialog::buildEntry() const
{
    core::Entry e;
    e.uuid         = QUuid::createUuid().toString(QUuid::WithoutBraces);
    e.title        = m_titleEdit->text().trimmed();
    e.creationTime = QDateTime::currentSecsSinceEpoch();
    return e;
}

} // namespace gui
