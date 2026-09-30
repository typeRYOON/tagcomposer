#pragma once
#include <QDialog>

class QKeyEvent;
class QShowEvent;

namespace tc {

class WindowChrome;

// Frameless dialog in the main window's chrome. Lay content into contentArea().
class ChromedDialog : public QDialog {
    Q_OBJECT

public:
    explicit ChromedDialog(QWidget* parent = nullptr);

    QWidget* contentArea() const;

    // Modal yes/no; true on confirm.
    static bool confirm(QWidget* parent, const QString& title, const QString& message,
                        const QString& confirmText = QStringLiteral("OK"),
                        const QString& cancelText = QStringLiteral("Cancel"));

public slots:
    // Fades out before closing.
    void done(int result) override;

protected:
    void changeEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    WindowChrome* m_chrome = nullptr;
    bool m_closing = false;
};

} // namespace tc
