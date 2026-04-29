#include <gui/tagsearchbar.h>
#include <utils/stringutils.h>
#include <QVBoxLayout>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QKeyEvent>
#include <QShowEvent>

// ── Delegate ──────────────────────────────────────────────────────────────────

namespace {

enum {
    CanonicalTagRole = Qt::UserRole,
    CountRole,
    CategoryRole,
    IsAliasRole,
    MatchStartRole,
    MatchLenRole,
};

static QColor categoryColor(int cat)
{
    switch (cat) {
    case 0:  return { 0xb4, 0xc7, 0xd9 }; // general   – light blue-gray
    case 1:  return { 0xf2, 0xac, 0x08 }; // artist    – orange
    case 3:  return { 0xdd, 0x00, 0xdd }; // copyright – purple
    case 4:  return { 0x00, 0xaa, 0x00 }; // character – green
    case 5:  return { 0xaa, 0xaa, 0xaa }; // meta      – gray
    default: return { 0xe0, 0xe0, 0xe0 };
    }
}

class TagDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        return { 0, 36 };
    }

    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& idx) const override
    {
        p->save();

        const bool sel = opt.state & QStyle::State_Selected;
        
        if (sel) {
            p->fillRect(opt.rect, QColor(0x22, 0x22, 0x22)); // bg (bug: not contained)
            p->fillRect(QRect(opt.rect.left(), opt.rect.top(), 3, opt.rect.height()),
                QColor(0x40, 0x80, 0xff)); // blue ticker
        }
            

        const QString display   = idx.data(Qt::DisplayRole).toString();
        const QString canonical = idx.data(CanonicalTagRole).toString();
        const int     cat       = idx.data(CategoryRole).toInt();
        const int64_t count     = idx.data(CountRole).toLongLong();
        const bool    isAlias   = idx.data(IsAliasRole).toBool();
        const int     mStart    = idx.data(MatchStartRole).toInt();
        const int     mLen      = idx.data(MatchLenRole).toInt();

        const QColor tagCol = categoryColor(cat);

        QFont baseF = opt.font;  baseF.setPointSize(11);
        QFont boldF = baseF;     boldF.setBold(true);
        QFontMetrics fmBase(baseF);

        const int baseline = opt.rect.top()
                           + (opt.rect.height() + fmBase.ascent() - fmBase.descent()) / 2;

        // Three segments: before match | matched (bold) | after match
        const QString pre  = display.left(mStart);
        const QString mid  = display.mid(mStart, mLen);
        const QString post = display.mid(mStart + mLen);

        int x = opt.rect.left() + 12;
        auto draw = [&](const QString& t, const QFont& f) {
            if (t.isEmpty()) return;
            p->setFont(f);
            p->setPen(tagCol);
            p->drawText(x, baseline, t);
            x += QFontMetrics(f).horizontalAdvance(t);
        };
        draw(pre,  baseF);
        draw(mid,  boldF);
        draw(post, baseF);

        // Pills drawn right-to-left: count first, alias chip second
        QFont pillF = opt.font;  pillF.setPointSize(9);
        QFontMetrics fmPill(pillF);

        int rx = opt.rect.right() - 8;
        auto drawPill = [&](const QString& text) {
            const int pw = fmPill.horizontalAdvance(text) + 14;
            const int ph = 17;
            const int py = opt.rect.top() + (opt.rect.height() - ph) / 2;
            rx -= pw;
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(0x2a, 0x2a, 0x2a));
            p->setRenderHint(QPainter::Antialiasing);
            p->drawRoundedRect(QRect(rx, py, pw, ph), 4, 4);
            p->setFont(pillF);
            p->setPen(QColor(0xcc, 0xcc, 0xcc));
            p->drawText(QRect(rx, py, pw, ph), Qt::AlignCenter, text);
            rx -= 4;
        };

        drawPill(QString::number(count));
        if (isAlias) drawPill(canonical);

        p->restore();
    }
};

} // anonymous namespace


// ── TagSearchBar ──────────────────────────────────────────────────────────────

namespace gui {

TagSearchBar::TagSearchBar(QWidget* parent)
    : QWidget(parent)
{
    m_input = new QLineEdit(this);
    m_input->setObjectName("TagSearchInput");
    m_input->installEventFilter(this);
    m_input->setPlaceholderText("search tags...");

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->addWidget(m_input);

    // Popup — frameless top-level, shown without stealing focus from m_input
    m_popup = new QFrame(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    m_popup->setObjectName("TagSearchPopup");
    m_popup->setAttribute(Qt::WA_ShowWithoutActivating);

    m_list = new QListWidget(m_popup);
    m_list->setObjectName("TagSearchList");
    m_list->setItemDelegate(new TagDelegate(m_list));
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setMouseTracking(true);
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem*) { commitCurrent(); });
    connect(m_list, &QListWidget::itemEntered, m_list,
        qOverload<QListWidgetItem*>(&QListWidget::setCurrentItem));

    auto* popupLayout = new QVBoxLayout(m_popup);
    popupLayout->setContentsMargins(0, 0, 0, 0);
    popupLayout->setSpacing(0);
    popupLayout->addWidget(m_list);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(120);
    connect(m_input, &QLineEdit::textChanged, this, &TagSearchBar::queryChanged);
    connect(m_input, &QLineEdit::textChanged, m_debounce, qOverload<>(&QTimer::start));
    connect(m_debounce, &QTimer::timeout, this, [this]() {
        runSearch(m_input->text().trimmed());
    });
}

void TagSearchBar::setIndex(core::DanbooruIndex* index)
{
    m_index = index;
}

void TagSearchBar::setActiveTags(const QSet<QString>* tags)
{
    m_activeTags = tags;
}

void TagSearchBar::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    if (auto* w = window(); w && w != this) {
        w->installEventFilter(this);
        if (m_popup->parent() != w) {
            m_popup->setParent(w, Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
            m_popup->setAttribute(Qt::WA_ShowWithoutActivating);
        }
    }
}

void TagSearchBar::runSearch(const QString& text)
{
    m_list->clear();
    if (!m_index || text.isEmpty()) { hidePopup(); return; }

    const auto results = m_index->search(text, 12);
    if (results.isEmpty()) { hidePopup(); return; }

    for (const auto& r : results) {
        auto* item = new QListWidgetItem(r.displayName);
        item->setData(CanonicalTagRole, r.canonicalTag);
        item->setData(CountRole,        (qlonglong)r.count);
        item->setData(CategoryRole,     r.category);
        item->setData(IsAliasRole,      r.isAlias);
        item->setData(MatchStartRole,   r.matchStart);
        item->setData(MatchLenRole,     r.matchLen);
        m_list->addItem(item);
    }

    const int rowH = 36;
    const int visH = rowH * std::min((int)results.size(), 8);
    m_list->setFixedHeight(visH);
    m_popup->setFixedSize(m_input->width(), visH);

    showPopup();
}

void TagSearchBar::showPopup()
{
    repositionPopup();
    m_popup->show();
    m_popup->raise();
}

void TagSearchBar::hidePopup()
{
    m_popup->hide();
}

void TagSearchBar::repositionPopup()
{
    m_popup->move(m_input->mapToGlobal(QPoint(0, m_input->height())));
}

void TagSearchBar::commitCurrent()
{
    QListWidgetItem* item = m_list->currentItem();
    if (!item) return;

    const QString canonical = item->data(CanonicalTagRole).toString();
    hidePopup();
    m_input->clear();

    if (m_activeTags && m_activeTags->contains(canonical))
        emit tagAlreadyPresent(canonical);
    else
        emit tagAdded(canonical);
}

void TagSearchBar::commitRaw(const QString& text)
{
    if (text.isEmpty()) return;
    hidePopup();
    m_input->clear();

    QSet<QString> batch;
    for (const QString& part : text.split(',')) {
        QString tag = part.trimmed();
        // Strip surrounding quote characters
        if (tag.size() >= 2) {
            const QChar f = tag.front(), b = tag.back();
            if ((f == '"' && b == '"') || (f == '\'' && b == '\''))
                tag = tag.mid(1, tag.size() - 2).trimmed();
        }
        if (tag.isEmpty() || batch.contains(tag)) continue;
        batch.insert(tag);
        if (m_activeTags && m_activeTags->contains(tag))
            emit tagAlreadyPresent(tag);
        else
            emit tagAdded(tag);
    }
}

bool TagSearchBar::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_input) {
        if (event->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(event);
            switch (ke->key()) {
            case Qt::Key_Down:
                if (!m_popup->isVisible()) showPopup();
                else m_list->setCurrentRow(std::min(m_list->currentRow() + 1, m_list->count() - 1));
                return true;
            case Qt::Key_Up:
                m_list->setCurrentRow(std::max(m_list->currentRow() - 1, 0));
                return true;
            case Qt::Key_Return:
            case Qt::Key_Enter:
            case Qt::Key_Tab: {
                const QString text = utils::normalizeTagInput(m_input->text());
                if (text.contains(',') || !m_popup->isVisible() || !m_list->currentItem())
                    commitRaw(text);
                else
                    commitCurrent();
                return true;
            }
            case Qt::Key_Escape:
                hidePopup();
                return true;
            }
        }
        if (event->type() == QEvent::FocusOut) {
            QTimer::singleShot(150, this, [this]() {
                if (!m_input->hasFocus()) hidePopup();
            });
        }
    } else {
        // Parent window moved/resized — keep popup anchored below input
        const auto t = event->type();
        if ((t == QEvent::Move || t == QEvent::Resize || t == QEvent::WindowStateChange)
                && m_popup->isVisible())
            repositionPopup();
    }
    return false;
}

} // namespace gui
