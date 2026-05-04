#pragma once
#include <QWidget>

class QLabel;
class QPushButton;
class QMouseEvent;

namespace gui {

// Custom titlebar for the frameless AppMainWindow. Drag the bar to move the
// window (delegates to QWindow::startSystemMove); double-click toggles
// maximize/restore. Min/max/close buttons mirror the native chrome.
//
// The bar pulls its label from window()->windowTitle() and keeps it in sync
// via an event filter on the top-level window - call setWindowTitle() on the
// main window as usual.
class TitleBar : public QWidget {
    Q_OBJECT
public:
    explicit TitleBar(QWidget* parent = nullptr);

    // Show/hide individual chrome buttons. Used by hosts like dialogs that
    // only want a close button (no minimize/maximize for modal contexts).
    void setButtons(bool showMin, bool showMax, bool showClose);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void toggleFullScreen();
    void refreshTitle();

    QLabel*      m_appIcon  = nullptr;
    QLabel*      m_title    = nullptr;
    QPushButton* m_minBtn   = nullptr;
    QPushButton* m_maxBtn   = nullptr;
    QPushButton* m_closeBtn = nullptr;
    QWidget*     m_watchedWindow = nullptr;
};

} // namespace gui
