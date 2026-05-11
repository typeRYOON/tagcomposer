#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QButtonGroup>
#include <QHash>
#include <QPixmap>

namespace gui {

// Indices into the main window's QStackedWidget. Adding or reordering a page
// means updating this enum, the stack's addWidget() order, and the navbar's
// addButton() order in lockstep.
enum class Page : int {
    Home = 0,
    EntryViewer = 1,
    TagComposer = 2,
    WorkflowEditor = 3,
    FacetEditor = 4,
    PromptHistory = 5,
    OutputViewer = 6,
    DatasetHelpers = 7,
    DanbooruWiki = 8,
    Settings = 9,
};

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
    QString m_tooltipText;
    QPixmap m_navIcon;
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
    QLabel* m_tooltip;
    QVector<NavButton*> m_buttons;
    QHash<int, NavButton*> m_pageButtonMap;
    void showTooltip(const QString& text, QPoint globalPos);
    void hideTooltip();
};

} // namespace gui
