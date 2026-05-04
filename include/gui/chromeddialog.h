#pragma once
#include <QDialog>

class QKeyEvent;
class QShowEvent;

namespace gui {

class WindowChrome;

// Frameless dialog with the same custom titlebar treatment as AppMainWindow:
// a 30 px chrome bar (close button only - modal flows don't need min/max),
// a thin cosmetic border, and outline-style edge-resize.
//
// Subclasses build their UI inside contentArea() instead of `this`:
//
//   auto* layout = new QVBoxLayout(contentArea());
//   layout->addWidget(...);
//
// The dialog still behaves like a normal QDialog - exec(), accept(), reject(),
// modality semantics all work - it just paints its own chrome.
class ChromedDialog : public QDialog {
    Q_OBJECT
public:
    explicit ChromedDialog(QWidget* parent = nullptr);

    // Widget that subclasses should layout into. Lives below the titlebar and
    // inside the cosmetic border.
    QWidget* contentArea() const;

public slots:
    // Routes both Accept and Reject through a fade-out animation, then defers
    // to QDialog::done(). Override of the base virtual; called by accept(),
    // reject(), and the default closeEvent.
    void done(int result) override;

protected:
    void changeEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    WindowChrome* m_chrome    = nullptr;
    bool          m_isClosing = false;
};

} // namespace gui
