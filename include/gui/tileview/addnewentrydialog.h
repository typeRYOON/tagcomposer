#pragma once
#include <core/entry.h>
#include <gui/chromeddialog.h>
#include <QLineEdit>

namespace gui {

class AddEntryDialog : public ChromedDialog {
    Q_OBJECT
public:
    explicit AddEntryDialog(QWidget* parent = nullptr);
    void setFields(const QString& title);
    core::Entry buildEntry() const;

private:
    QLineEdit* m_titleEdit;
};

} // namespace gui
