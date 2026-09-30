#include <app/entry_viewer_page.h>
#include <app/entry_panel.h>
#include <app/entry_view.h>
#include <core/composer_store.h>
#include <QColor>
#include <QHBoxLayout>
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

constexpr int kDebounceMs = 180;

} // namespace

EntryViewerPage::EntryViewerPage(EntryStore& store, EntrySearch& search,
                                 ComposerStore& composer, const Settings& settings,
                                 ComfyClient& comfy, QWidget* parent)
    : QWidget(parent), m_store(&store), m_search(&search), m_settings(&settings)
{
    setObjectName(u"TileViewPage"_s);
    setAttribute(Qt::WA_StyledBackground, true);

    m_query = new QLineEdit;
    m_query->setObjectName(u"SearchBar"_s);
    m_query->setPlaceholderText(u"Filter your entries"_s);

    m_view = new EntryView(*m_store);
    m_panel = new EntryPanel(*m_store, composer, settings, comfy);

    auto* left = new QVBoxLayout;
    left->setContentsMargins(0, 0, 0, 0);
    left->setSpacing(0);
    left->addWidget(m_query);
    left->addWidget(m_view, 1);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addLayout(left, 1);
    root->addWidget(m_panel);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDebounceMs);
    connect(m_debounce, &QTimer::timeout, this, &EntryViewerPage::runQuery);
    connect(m_query, &QLineEdit::textChanged, this, [this]() { m_debounce->start(); });

    connect(m_view, &EntryView::focusFilterRequested, m_query,
            qOverload<>(&QLineEdit::setFocus));
    connect(m_view, &EntryView::entryClicked, this, &EntryViewerPage::entrySelected);
    connect(m_view, &EntryView::entryClicked, m_panel, &EntryPanel::setEntry);

    // Clicks focus the tag input; arrow keys keep focus on the grid.
    connect(m_view, &EntryView::entryClickedByPointer, m_panel, &EntryPanel::focusTagInput);

    // Enter toggles the push for the selected tile.
    connect(m_view, &EntryView::entryActivated, this, [this](const QString& uuid) {
        m_panel->setEntry(uuid);
        m_panel->toggleComposerPush();
    });
    // Grid keys pressed in the panel move focus to the grid and replay there.
    connect(m_panel, &EntryPanel::gridNavRequested, this, [this](int key) {
        m_view->setFocus(Qt::OtherFocusReason);
        QKeyEvent replay(QEvent::KeyPress, key, Qt::NoModifier);
        QApplication::sendEvent(m_view, &replay);
    });

    m_query->installEventFilter(this);

    connect(m_panel, &EntryPanel::statusMessage, this, &EntryViewerPage::statusMessage);
    connect(m_panel, &EntryPanel::entryDeleted, this, [this]() { m_view->setSelected(QString()); });

    // Tile rings and the nav float follow the composer document.
    auto syncActive = [this, &composer]() {
        QSet<QString> pushed;
        for (const EntryPush& push : composer.doc().pushes)
            pushed.insert(push.entryUuid);
        m_view->setActiveEntries(pushed);

        QSet<QString> loras;
        for (const Lora& lora : composer.doc().loraStack) {
            for (const Entry& entry : m_store->all()) {
                if (!entry.lora || entry.lora->sha256 != lora.sha256) continue;
                loras.insert(entry.uuid);
                break;
            }
        }
        m_view->setActiveLoras(loras);
    };
    connect(&composer, &ComposerStore::docChanged, this, syncActive);
    syncActive();

    // Tile menu LoRA toggle: append, or remove in place to keep stack order.
    connect(m_view, &EntryView::loraToggled, this, [this, &composer](const QString& uuid) {
        const Entry* entry = m_store->find(uuid);
        if (!entry || !entry->lora) return;

        QList<Lora> stack = composer.doc().loraStack;
        const QString sha = entry->lora->sha256;

        qsizetype at = -1;
        for (qsizetype i = 0; i < stack.size(); ++i)
            if (stack[i].sha256 == sha) at = i;

        if (at >= 0)
            stack.removeAt(at);
        else
            stack << *entry->lora;

        composer.setLoraStack(std::move(stack));
    });

    // Library changes can change what the query matches.
    connect(m_store, &EntryStore::reloaded, this, &EntryViewerPage::runQuery);
    connect(m_store, &EntryStore::entryChanged, this, [this]() { m_debounce->start(); });
    connect(m_store, &EntryStore::entryRemoved, this, [this]() { m_debounce->start(); });
    connect(m_store, &EntryStore::entryAdded, this, [this]() { m_debounce->start(); });

    runQuery();
}

void EntryViewerPage::applySettings()
{
    m_view->setTileGradient(m_settings->tileGradientStart, m_settings->tileGradientAlpha);
    m_view->setTileTitleColor(QColor(m_settings->tileTitleColor));
    m_panel->applySettings();
}

void EntryViewerPage::setDanbooruIndex(const DanbooruIndex* index)
{
    m_panel->setDanbooruIndex(index);
}

void EntryViewerPage::showEntry(const QString& uuid)
{
    if (!m_query->text().isEmpty()) {
        const QSignalBlocker block(m_query);
        m_query->clear();
    }
    m_debounce->stop();
    runQuery();

    m_view->setSelected(uuid);
    m_view->scrollToUuid(uuid);
    m_panel->setEntry(uuid);
}

void EntryViewerPage::runQuery()
{
    m_view->setEntries(m_search->find(m_query->text()));
}

bool EntryViewerPage::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_query || event->type() != QEvent::KeyPress)
        return QWidget::eventFilter(watched, event);

    // Down moves focus into the grid and replays the key there.
    if (static_cast<QKeyEvent*>(event)->key() != Qt::Key_Down)
        return QWidget::eventFilter(watched, event);

    m_view->setFocus(Qt::OtherFocusReason);
    QKeyEvent replay(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QApplication::sendEvent(m_view, &replay);
    return true;
}

} // namespace tc
