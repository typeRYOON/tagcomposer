#pragma once
#include <QWidget>

class QLabel;
class QPushButton;
class QMouseEvent;

namespace tc {

// Frameless titlebar: drag moves, double-click toggles fullscreen. Follows the
// window's title.
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
