#pragma once
#include <QDialog>

class QKeyEvent;
class QShowEvent;

namespace tc {

class WindowChrome;

// Frameless dialog wearing the same chrome as the main window: a 30px bar
// with only a close button, a thin border, and outline edge resize.
//
// Subclasses lay out into contentArea(), not into `this`. Everything else
// about QDialog still holds - exec(), accept(), reject(), modality.
class ChromedDialog : public QDialog {
    Q_OBJECT

public:
    explicit ChromedDialog(QWidget* parent = nullptr);

    // Below the titlebar, inside the border. Lay content in here.
    QWidget* contentArea() const;

    // Modal yes/no in this chrome. True on confirm.
    static bool confirm(QWidget* parent, const QString& title, const QString& message,
                        const QString& confirmText = QStringLiteral("OK"),
                        const QString& cancelText = QStringLiteral("Cancel"));

public slots:
    // Both accept and reject fade out first, then hand off to QDialog.
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
