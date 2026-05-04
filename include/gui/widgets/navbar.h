#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QButtonGroup>
#include <QHash>
#include <QPixmap>

namespace gui {

// Page identifiers for the main window's QStackedWidget. Values are the index
// into the stack - adding a page means appending here AND inserting an
// addWidget()/addButton() in the matching visual position. Reordering means
// touching all three. Keeps page references self-documenting at the call site.
enum class Page : int {
    Home           = 0,
    EntryViewer    = 1,
    TagComposer    = 2,
    FacetEditor    = 3,
    WorkflowEditor = 4,
    OutputViewer   = 5,
    DatasetHelpers = 6,
    DanbooruWiki   = 7,
    Settings       = 8,
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
