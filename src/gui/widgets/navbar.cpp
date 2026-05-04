#include <gui/widgets/navbar.h>
#include <QVBoxLayout>
#include <QEnterEvent>
#include <QPainter>
#include <QImage>

namespace gui {

NavButton::NavButton(const QString& label, const QString& tooltipText, QWidget* parent)
    : QPushButton(label, parent), m_tooltipText(tooltipText)
{
    setObjectName("NavButton");
    setCheckable(true);
    setFixedSize(44, 44);
    setCursor(Qt::PointingHandCursor);
}

void NavButton::setNavIcon(const QPixmap& px)
{
    m_navIcon = px;
    setText("");
    update();
}

void NavButton::enterEvent(QEnterEvent* e)
{
    QPushButton::enterEvent(e);
    emit hovered(m_tooltipText, mapToGlobal(QPoint(width(), height() / 2)));
}

void NavButton::leaveEvent(QEvent* e)
{
    QPushButton::leaveEvent(e);
    emit unhovered();
}

void NavButton::paintEvent(QPaintEvent* e)
{
    QPushButton::paintEvent(e); // draws QSS background / border-radius

    if (m_navIcon.isNull()) return;

    // Tint colour based on state
    QColor tint;
    if (isChecked())
        tint = QColor(0xff, 0xff, 0xff);
    else if (underMouse())
        tint = QColor(0xcc, 0xcc, 0xcc);
    else
        tint = QColor(0x66, 0x66, 0x66);

    // Re-colour: keep the icon's alpha channel, replace RGB with tint
    QImage img = m_navIcon.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    {
        QPainter t(&img);
        t.setCompositionMode(QPainter::CompositionMode_SourceIn);
        t.fillRect(img.rect(), tint);
    }

    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    constexpr int sz = 22;
    const QRect r((width() - sz) / 2, (height() - sz) / 2, sz, sz);
    p.drawImage(r, img);
}


NavBar::NavBar(QWidget* tooltipParent, QWidget* parent) : QWidget(parent)
{
    setObjectName("NavBar");
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(60);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    layout->setAlignment(Qt::AlignTop);

    m_tooltip = new QLabel(tooltipParent);
    m_tooltip->setObjectName("NavTooltip");
    m_tooltip->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_tooltip->hide();

    QButtonGroup* group = new QButtonGroup(this);
    group->setExclusive(true);

    auto addButton = [&](Page page, const QString& tip,
                         const QString& iconPath = {}) -> NavButton* {
        const int pageIdx = int(page);
        NavButton* btn = new NavButton(QString::number(pageIdx), tip, this);
        layout->addWidget(btn, 0, Qt::AlignHCenter);
        group->addButton(btn);
        m_buttons.append(btn);
        m_pageButtonMap[pageIdx] = btn;
        connect(btn, &NavButton::hovered, this, &NavBar::showTooltip);
        connect(btn, &NavButton::unhovered, this, &NavBar::hideTooltip);
        connect(btn, &QPushButton::clicked, this,
                [this, pageIdx]() { emit pageRequested(pageIdx); });
        if (!iconPath.isEmpty()) {
            QPixmap px(iconPath);
            if (!px.isNull()) btn->setNavIcon(px);
        }
        return btn;
    };

    addButton(Page::Home, "Home", ":/icons/nav_home.png")->setChecked(true);
    addButton(Page::EntryViewer, "Entry Viewer", ":/icons/nav_tiles.png");
    addButton(Page::TagComposer, "Tag Composer", ":/icons/nav_composer.png");
    addButton(Page::FacetEditor, "Facet Editor", ":/icons/nav_facets.png");
    addButton(Page::WorkflowEditor, "Workflow Editor", ":/icons/nav_workflow.png");
    // Reuses nav_tiles.png until a dedicated nav_output.png ships - drop one
    // into resources/icons/, register it in resources.qrc, and update this path.
    addButton(Page::OutputViewer, "Output Viewer", ":/icons/nav_tiles.png");
    addButton(Page::DatasetHelpers, "Dataset Helpers", ":/icons/nav_dataset.png");
    addButton(Page::DanbooruWiki, "Danbooru Wiki", ":/icons/nav_wiki.png");
    layout->addStretch();
    addButton(Page::Settings, "Settings", ":/icons/nav_settings.png");
}

void NavBar::showTooltip(const QString& text, QPoint globalPos)
{
    QWidget* p = m_tooltip->parentWidget();
    m_tooltip->setText(text);
    m_tooltip->adjustSize();

    const QPoint local = p->mapFromGlobal(globalPos);
    m_tooltip->move(local.x() + 12, local.y() - m_tooltip->height() / 2);
    m_tooltip->raise();
    m_tooltip->show();
}

void NavBar::hideTooltip()
{
    m_tooltip->hide();
}

void NavBar::setCurrentPage(int index)
{
    if (auto* btn = m_pageButtonMap.value(index, nullptr)) btn->setChecked(true);
}

} // namespace gui
