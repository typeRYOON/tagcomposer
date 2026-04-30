#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QButtonGroup>
#include <QHash>
#include <QPixmap>

namespace gui {

class NavButton : public QPushButton {
    Q_OBJECT
public:
    explicit NavButton(const QString& label, const QString& tooltipText, QWidget* parent = nullptr);

    void setNavIcon(const QPixmap& px);

signals:
    void hovered(const QString& text, QPoint globalPos);
    void unhovered();

protected:
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void paintEvent(QPaintEvent* e) override;

private:
    QString  m_tooltipText;
    QPixmap  m_navIcon;
};


class NavBar : public QWidget {
    Q_OBJECT
public:
    explicit NavBar(QWidget* tooltipParent, QWidget* parent = nullptr);

public slots:
    void setCurrentPage(int index);

signals:
    void pageRequested(int index);

private:
    QLabel*                 m_tooltip;
    QVector<NavButton*>     m_buttons;
    QHash<int, NavButton*>  m_pageButtonMap;
    void showTooltip(const QString& text, QPoint globalPos);
    void hideTooltip();
};

} // namespace gui
