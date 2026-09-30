#pragma once
#include <QHash>
#include <QPixmap>
#include <QPushButton>
#include <QWidget>

class QLabel;

namespace tc {

// Navbar button with per-state icon tints, rendered once.
class NavButton : public QPushButton {
    Q_OBJECT

public:
    NavButton(const QString& tooltip, QWidget* parent = nullptr);

    void setNavIcon(const QPixmap& pixmap);

signals:
    void hovered(const QString& text, QPoint globalPos);
    void unhovered();

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_tooltip;
    QPixmap m_idle;
    QPixmap m_hover;
    QPixmap m_active;
};

// Buttons come from kPages. The tooltip lives outside the bar so it can overhang.
class NavBar : public QWidget {
    Q_OBJECT

public:
    explicit NavBar(QWidget* tooltipParent, QWidget* parent = nullptr);

public slots:
    void setCurrentPage(int index);

signals:
    void pageRequested(int index);
    void discordRequested();

private:
    NavButton* addButton(const QString& tooltip, const QString& iconPath);
    void showTooltip(const QString& text, QPoint globalPos);

    QLabel* m_tooltip = nullptr;
    QHash<int, NavButton*> m_byPage;
};

} // namespace tc
