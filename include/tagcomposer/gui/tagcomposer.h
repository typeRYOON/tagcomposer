#pragma once
#include <QMainWindow>
#include <QCloseEvent>

namespace tagcomposer {

class AppMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit AppMainWindow(QWidget* parent = nullptr);
    ~AppMainWindow() = default;

    AppMainWindow(const AppMainWindow&) = delete;
    AppMainWindow& operator=(const AppMainWindow&) = delete;
    AppMainWindow(AppMainWindow&&) = delete;
    AppMainWindow& operator=(AppMainWindow&&) = delete;

protected:
    void closeEvent(QCloseEvent* event) override;


private:
    const QString m_appPath;


};

}
