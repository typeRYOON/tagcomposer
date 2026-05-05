#include <gui/tileview/addnewentrydialog.h>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QUuid>
#include <QDateTime>


namespace gui {

AddEntryDialog::AddEntryDialog(QWidget* parent) : ChromedDialog(parent)
{
    setWindowTitle("New Entry");
    setMinimumWidth(360);

    m_titleEdit = new QLineEdit(contentArea());
    m_titleEdit->setPlaceholderText("Entry title");

    auto* form = new QFormLayout;
    form->addRow("Title:", m_titleEdit);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, contentArea());
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(contentArea());
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(12);
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
    e.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    e.title = m_titleEdit->text().trimmed();
    e.creationTime = QDateTime::currentSecsSinceEpoch();
    return e;
}

} // namespace gui
