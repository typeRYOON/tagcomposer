#include <app/tag_search_bar.h>
#include <app/widget_utils.h>
#include <core/danbooru_index.h>
#include <core/entry.h>
#include <QApplication>
#include <QFrame>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QShowEvent>
#include <QStyledItemDelegate>
#include <QTextLayout>
#include <QTextOption>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

enum Role {
    CanonicalTagRole = Qt::UserRole,
    CountRole,
    CategoryRole,
    IsAliasRole,
    MatchStartRole,
    MatchLengthRole,
};

constexpr int kRowHeight = 36;
constexpr int kMaxVisibleRows = 8;
constexpr int kMaxResults = 30;

class TagDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        return {0, kRowHeight};
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        painter->save();

        if (option.state & QStyle::State_Selected) {
            painter->fillRect(option.rect, QColor(0x22, 0x22, 0x22));
            painter->fillRect(QRect(option.rect.left(), option.rect.top(), 3, option.rect.height()),
                              QColor(0x40, 0x80, 0xff));
        }

        const QString display = index.data(Qt::DisplayRole).toString();
        const QString canonical = index.data(CanonicalTagRole).toString();
        const int category = index.data(CategoryRole).toInt();
        const qint64 count = index.data(CountRole).toLongLong();
        const bool isAlias = index.data(IsAliasRole).toBool();

        QFont baseFont = option.font;
        baseFont.setPointSize(11);
        const QFontMetrics metrics(baseFont);
        const int baseline =
            option.rect.top() + (option.rect.height() + metrics.ascent() - metrics.descent()) / 2;

        // The whole string is laid out once and the match bolded as a format
        // range. Drawing three separate runs and advancing x by each one's
        // width drifts - integer rounding, metrics resolved against the app
        // default device rather than the painter's, and no kerning across the
        // boundary - which shows as a gap or an overlap where the bold ends.
        const int from = qBound(0, index.data(MatchStartRole).toInt(), int(display.size()));
        const int length =
            qBound(0, index.data(MatchLengthRole).toInt(), int(display.size()) - from);

        QTextLayout layout(display, baseFont, painter->device());
        QTextOption textOption;
        textOption.setWrapMode(QTextOption::NoWrap);
        layout.setTextOption(textOption);

        if (length > 0) {
            QTextLayout::FormatRange range;
            range.start = from;
            range.length = length;
            range.format.setFontWeight(QFont::Bold);
            layout.setFormats({range});
        }

        layout.beginLayout();
        QTextLine line = layout.createLine();
        line.setLineWidth(option.rect.width());
        layout.endLayout();

        painter->setPen(danbooruCategoryColor(category, QColor(0xe0, 0xe0, 0xe0)));
        layout.draw(painter, QPointF(option.rect.left() + 12, baseline - line.ascent()));

        // Pills run right to left: the count first, then the alias target.
        QFont pillFont = option.font;
        pillFont.setPointSize(9);
        const QFontMetrics pillMetrics(pillFont);

        int right = option.rect.right() - 8;
        auto drawPill = [&](const QString& text) {
            const int width = pillMetrics.horizontalAdvance(text) + 14;
            constexpr int height = 17;
            const int top = option.rect.top() + (option.rect.height() - height) / 2;
            right -= width;

            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0x2a, 0x2a, 0x2a));
            painter->setRenderHint(QPainter::Antialiasing);
            painter->drawRoundedRect(QRect(right, top, width, height), 4, 4);

            painter->setFont(pillFont);
            painter->setPen(QColor(0xcc, 0xcc, 0xcc));
            painter->drawText(QRect(right, top, width, height), Qt::AlignCenter, text);
            right -= 4;
        };

        drawPill(QString::number(count));
        if (isAlias) drawPill(canonical);

        painter->restore();
    }
};

QListWidget* makeResultList(QWidget* parent)
{
    auto* list = new QListWidget(parent);
    list->setObjectName(u"TagSearchList"_s);
    list->setItemDelegate(new TagDelegate(list));
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setFocusPolicy(Qt::NoFocus);
    list->setMouseTracking(true);
    return list;
}

void fillResults(QListWidget* list, const QList<TagSearchResult>& results)
{
    for (const TagSearchResult& result : results) {
        auto* item = new QListWidgetItem(result.displayName);
        item->setData(CanonicalTagRole, result.canonicalTag);
        item->setData(CountRole, qlonglong(result.count));
        item->setData(CategoryRole, result.category);
        item->setData(IsAliasRole, result.isAlias);
        item->setData(MatchStartRole, result.matchStart);
        item->setData(MatchLengthRole, result.matchLength);
        list->addItem(item);
    }
}

} // namespace

TagSearchBar::TagSearchBar(QWidget* parent) : QWidget(parent)
{
    m_input = new QLineEdit(this);
    m_input->setObjectName(u"TagSearchInput"_s);
    m_input->installEventFilter(this);
    m_input->setPlaceholderText(u"search tags..."_s);
    setFocusProxy(m_input);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(m_input);

    // A frameless top level shown without taking focus from the input.
    m_popup = new QFrame(nullptr,
                         Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    m_popup->setObjectName(u"TagSearchPopup"_s);
    m_popup->setAttribute(Qt::WA_ShowWithoutActivating);

    m_list = makeResultList(m_popup);
    connect(m_list, &QListWidget::itemClicked, this,
            [this](QListWidgetItem*) { commitCurrent(); });
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
    connect(m_debounce, &QTimer::timeout, this,
            [this]() { runSearch(m_input->text().trimmed()); });
}

void TagSearchBar::setIndex(const DanbooruIndex* index)
{
    m_index = index;
}

void TagSearchBar::setActiveTags(const QSet<QString>* tags)
{
    m_activeTags = tags;
}

void TagSearchBar::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    QWidget* host = window();
    if (!host || host == this) return;

    host->installEventFilter(this);
    if (m_popup->parent() == host) return;

    m_popup->setParent(host,
                       Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    m_popup->setAttribute(Qt::WA_ShowWithoutActivating);
}

void TagSearchBar::runSearch(const QString& text)
{
    m_list->clear();
    if (!m_index || text.isEmpty()) {
        hidePopup();
        return;
    }

    const QList<TagSearchResult> results = m_index->search(text, kMaxResults);
    if (results.isEmpty()) {
        hidePopup();
        return;
    }

    fillResults(m_list, results);

    const int height = kRowHeight * int(std::min<qsizetype>(results.size(), kMaxVisibleRows));
    m_list->setFixedHeight(height);
    m_popup->setFixedSize(m_input->width(), height);
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
    for (const QString& part : text.split(u',')) {
        QString tag = part.trimmed();

        // A pasted tag often arrives quoted.
        if (tag.size() >= 2) {
            const QChar front = tag.front();
            const QChar back = tag.back();
            if ((front == u'"' && back == u'"') || (front == u'\'' && back == u'\''))
                tag = tag.sliced(1, tag.size() - 2).trimmed();
        }

        if (tag.isEmpty() || batch.contains(tag)) continue;
        batch.insert(tag);

        if (m_activeTags && m_activeTags->contains(tag))
            emit tagAlreadyPresent(tag);
        else
            emit tagAdded(tag);
    }
}

bool TagSearchBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_input) {
        // The window moved or resized, so the popup has to follow the input.
        const QEvent::Type type = event->type();
        if ((type == QEvent::Move || type == QEvent::Resize
             || type == QEvent::WindowStateChange)
            && m_popup->isVisible())
            repositionPopup();
        return false;
    }

    if (event->type() == QEvent::FocusOut) {
        QTimer::singleShot(150, this, [this]() {
            if (!m_input->hasFocus()) hidePopup();
        });
    }

    if (event->type() != QEvent::KeyPress) return false;
    auto* key = static_cast<QKeyEvent*>(event);

    switch (key->key()) {
    case Qt::Key_Down:
        if (m_popup->isVisible()) {
            m_list->setCurrentRow(std::min(m_list->currentRow() + 1, m_list->count() - 1));
        } else if (m_list->count() > 0) {
            showPopup(); // a populated popup that a focus-out had hidden
        } else if (m_input->text().isEmpty()) {
            emit downArrowOnEmpty();
        }
        return true;

    case Qt::Key_Up:
        m_list->setCurrentRow(std::max(m_list->currentRow() - 1, 0));
        return true;

    case Qt::Key_Tab:
        // Only commits when the user actually arrowed onto a result.
        // Otherwise Tab stays focus traversal, so chained tabs from a
        // neighbouring widget do not commit something unintended.
        if (m_popup->isVisible() && m_list->currentItem()) {
            commitCurrent();
            return true;
        }
        break;

    case Qt::Key_Return:
    case Qt::Key_Enter: {
        const QString text = normalizeTag(m_input->text());
        if (text.contains(u',') || !m_popup->isVisible() || !m_list->currentItem())
            commitRaw(text);
        else
            commitCurrent();
        return true;
    }

    case Qt::Key_Escape:
        // With the popup up, Escape closes it. Otherwise it is a focus
        // gesture, and it is consumed either way so it never reaches a
        // window-level handler such as leaving fullscreen.
        if (m_popup->isVisible()) {
            hidePopup();
            return true;
        }
        emit escapePressed();
        return true;

    default:
        break;
    }
    return false;
}

TagLineAutocomplete::TagLineAutocomplete(QLineEdit* edit, const DanbooruIndex* index,
                                         QObject* parent)
    : QObject(parent ? parent : edit), m_edit(edit), m_index(index)
{
    m_edit->installEventFilter(this);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(120);
    connect(m_edit, &QLineEdit::textChanged, m_debounce, qOverload<>(&QTimer::start));
    connect(m_debounce, &QTimer::timeout, this, [this]() { runSearch(m_edit->text().trimmed()); });
}

void TagLineAutocomplete::ensurePopup()
{
    if (m_popup) return;

    // Parented to the edit rather than its window: the composer rebuilds its
    // inline tag rows on every rename, and a window-parented popup would
    // orphan a visible widget when the row goes away mid-click.
    //
    // WindowDoesNotAcceptFocus is what makes clicking the popup work at all.
    // Without it a Qt::Tool window activates on click on Windows, stealing
    // focus from the edit and firing editingFinished with the half-typed
    // text before commitSelection gets to substitute the canonical tag.
    m_popup = new QFrame(m_edit, Qt::Tool | Qt::FramelessWindowHint
                                     | Qt::NoDropShadowWindowHint
                                     | Qt::WindowDoesNotAcceptFocus);
    m_popup->setObjectName(u"TagSearchPopup"_s);
    m_popup->setAttribute(Qt::WA_ShowWithoutActivating);

    m_list = makeResultList(m_popup);
    connect(m_list, &QListWidget::itemClicked, this,
            [this](QListWidgetItem*) { commitSelection(); });
    connect(m_list, &QListWidget::itemEntered, m_list,
            qOverload<QListWidgetItem*>(&QListWidget::setCurrentItem));

    auto* layout = new QVBoxLayout(m_popup);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_list);
}

void TagLineAutocomplete::runSearch(const QString& text)
{
    if (!m_index || text.isEmpty() || !m_edit->hasFocus()) {
        hidePopup();
        return;
    }

    ensurePopup();
    m_list->clear();

    const QList<TagSearchResult> results = m_index->search(text, kMaxResults);
    if (results.isEmpty()) {
        hidePopup();
        return;
    }

    fillResults(m_list, results);

    const int height = kRowHeight * int(std::min<qsizetype>(results.size(), kMaxVisibleRows));
    m_list->setFixedHeight(height);
    m_popup->setFixedSize(qMax(m_edit->width(), 240), height);
    showPopup();
}

void TagLineAutocomplete::showPopup()
{
    if (!m_popup) return;

    repositionPopup();
    m_popup->show();
    m_popup->raise();

    // A focus-out alone misses a click that lands on a non-focusable widget
    // or another window, so outside clicks are caught application-wide.
    qApp->installEventFilter(this);
}

void TagLineAutocomplete::hidePopup()
{
    if (m_popup) m_popup->hide();
    qApp->removeEventFilter(this);
}

void TagLineAutocomplete::repositionPopup()
{
    if (!m_popup) return;
    m_popup->move(m_edit->mapToGlobal(QPoint(0, m_edit->height())));
}

void TagLineAutocomplete::commitSelection()
{
    if (!m_list) return;

    QListWidgetItem* item = m_list->currentItem();
    if (!item) return;

    hidePopup();
    m_edit->setText(item->data(CanonicalTagRole).toString());

    // setText restarts the debounce whenever the canonical form differs from
    // what was typed, so cancel it or the popup reopens 120ms later.
    m_debounce->stop();

    // clearFocus fires editingFinished, which runs the host's rename handler
    // exactly as if this had been typed and entered.
    m_edit->clearFocus();
}

bool TagLineAutocomplete::eventFilter(QObject* watched, QEvent* event)
{
    // Any press outside both the popup and the edit closes the popup. The
    // event is not consumed: the click should still reach its target.
    if ((event->type() == QEvent::MouseButtonPress
         || event->type() == QEvent::NonClientAreaMouseButtonPress)
        && m_popup && m_popup->isVisible()) {
        const QPoint global = static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
        const QRect popupRect(m_popup->mapToGlobal(QPoint(0, 0)), m_popup->size());
        const QRect editRect(m_edit->mapToGlobal(QPoint(0, 0)), m_edit->size());
        if (!popupRect.contains(global) && !editRect.contains(global)) hidePopup();
    }

    if (watched != m_edit) return false;

    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        const bool popupOpen = m_popup && m_popup->isVisible();

        switch (key->key()) {
        case Qt::Key_Down:
            if (!popupOpen) break;
            m_list->setCurrentRow(std::min(m_list->currentRow() + 1, m_list->count() - 1));
            return true;

        case Qt::Key_Up:
            if (!popupOpen) break;
            m_list->setCurrentRow(std::max(m_list->currentRow() - 1, 0));
            return true;

        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Tab:
            // With a live selection, commit it. Otherwise dismiss and let
            // the edit's own editingFinished commit whatever was typed.
            if (!popupOpen) break;
            if (m_list->currentItem()) {
                commitSelection();
                return true;
            }
            hidePopup();
            break;

        case Qt::Key_Escape:
            if (!popupOpen) break;
            hidePopup();
            return true;

        default:
            break;
        }
    }

    if (event->type() == QEvent::FocusOut) {
        // Deferred, because click delivery races focus transfer on some
        // styles and the focus may be heading into the popup.
        QTimer::singleShot(150, this, [this]() {
            if (!m_edit->hasFocus()) hidePopup();
        });
    }

    // A page switch hides the edit through its parent. The popup is its own
    // top-level window, so it would otherwise linger on screen.
    if (event->type() == QEvent::Hide || event->type() == QEvent::HideToParent) hidePopup();

    return false;
}

} // namespace tc
