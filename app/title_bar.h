#pragma once
#include <QWidget>

class QLabel;
class QPushButton;
class QMouseEvent;

namespace tc {

// Titlebar for a frameless window. Dragging moves the window via
// QWindow::startSystemMove, double-click toggles fullscreen.
//
// The label tracks window()->windowTitle() through an event filter, so hosts
// just call setWindowTitle() as usual.
class TitleBar : public QWidget {
    Q_OBJECT

public:
    explicit TitleBar(QWidget* parent = nullptr);

    void setButtons(bool showMin, bool showMax, bool showClose);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void toggleFullScreen();
    void refreshTitle();

    QLabel* m_appIcon = nullptr;
    QLabel* m_title = nullptr;
    QPushButton* m_minButton = nullptr;
    QPushButton* m_maxButton = nullptr;
    QPushButton* m_closeButton = nullptr;
    QWidget* m_watched = nullptr;
};

} // namespace tc
