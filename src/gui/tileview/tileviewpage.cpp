#include <gui/tileview/tileviewpage.h>
#include <gui/tileview/entryview.h>
#include <gui/tileview/entrypanel.h>
#include <QVBoxLayout>
#include <QFontDatabase>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QFutureWatcher>
#include <QtConcurrent>

namespace {

class LoraDropSearchBar : public QLineEdit {
public:
    explicit LoraDropSearchBar(QWidget* parent = nullptr) : QLineEdit(parent)
    {
        setAcceptDrops(true);
    }

protected:
    void dragEnterEvent(QDragEnterEvent* e) override
    {
        if (!e->mimeData()->hasUrls()) return;
        const QString suffix =
            QFileInfo(e->mimeData()->urls().first().toLocalFile()).suffix().toLower();
        if (suffix == "safetensors") e->acceptProposedAction();
    }
    void dropEvent(QDropEvent* e) override
    {
        const QString path = e->mimeData()->urls().first().toLocalFile();
        if (path.isEmpty()) return;

        setEnabled(false);
        setPlaceholderText("hashing LoRA...");

        auto* watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher]() {
            watcher->deleteLater();
            const QString hash = watcher->result();
            setEnabled(true);
            setPlaceholderText("search tags...");
            if (!hash.isEmpty())
                setText("lora:" + hash);
            else
                clear();
        });
        watcher->setFuture(QtConcurrent::run([path]() -> QString {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) return {};
            QCryptographicHash h(QCryptographicHash::Sha256);
            h.addData(&f);
            return h.result().toHex();
        }));
    }
};

} // anonymous namespace

namespace gui {

TileViewPage::TileViewPage(core::EntryModel* model, QWidget* parent) : QWidget(parent)
{
    setObjectName("TileViewPage");
    setAttribute(Qt::WA_StyledBackground, true);


    m_searchBar = new LoraDropSearchBar(this);
    m_searchBar->setObjectName("SearchBar");
    m_searchBar->setPlaceholderText("search tags...");
    const QStringList families = QFontDatabase::applicationFontFamilies(0);
    if (!families.isEmpty()) {
        m_searchBar->setFont(QFont(families.first()));
    }

    m_entryView = new EntryView(model, this);
    m_entryPanel = new EntryPanel(model, this);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(150);
    connect(m_searchBar, &QLineEdit::textChanged, m_debounce, qOverload<>(&QTimer::start));
    connect(m_debounce, &QTimer::timeout, this,
            [this]() { m_entryView->query(m_searchBar->text()); });

    connect(m_entryView, &EntryView::entryClicked, m_entryPanel, &EntryPanel::setEntry);
    // Pointer-driven selections (mouse, nav panel, external requests) also
    // park focus in the search bar; keyboard arrow nav deliberately skips
    // this so the tile view keeps focus for chained arrow presses.
    connect(m_entryView, &EntryView::entryClickedByPointer, m_entryPanel,
            &EntryPanel::focusSearchInput);

    // selectAndScrollToEntry emits entryClicked, which routes back to setEntry above.
    connect(m_entryPanel, &EntryPanel::entrySelectRequested, m_entryView,
            &EntryView::selectAndScrollToEntry);

    connect(m_entryPanel, &EntryPanel::tagsExported, this, &TileViewPage::tagsExported);
    connect(m_entryView, &EntryView::tagsExported, this, &TileViewPage::tagsExported);
    connect(m_entryPanel, &EntryPanel::wikiRequested, this, &TileViewPage::wikiRequested);
    connect(m_entryPanel, &EntryPanel::facetEditorRequested, this,
            &TileViewPage::facetEditorRequested);
    connect(m_entryPanel, &EntryPanel::quickFacetRequested, this,
            &TileViewPage::quickFacetRequested);
    connect(m_entryPanel, &EntryPanel::statusMessageRequested, this,
            &TileViewPage::statusMessageRequested);

    connect(m_entryView, &EntryView::loraStackChanged, this, &TileViewPage::loraStackChanged);
    connect(m_entryPanel, &EntryPanel::loraCleared, m_entryView, &EntryView::clearLoraForEntry);

    connect(m_entryPanel, &EntryPanel::entryTagAdded, this,
            [this, model](int32_t id, int img, const QString& tag) {
                model->addTagToImage(id, img, tag);
                m_entryView->query(m_searchBar->text());
                emit entryTagAdded(int(id), img, tag);
            });
    connect(m_entryPanel, &EntryPanel::entryTagRemoved, this,
            [this, model](int32_t id, int img, const QString& tag) {
                model->removeTagFromImage(id, img, tag);
                m_entryView->query(m_searchBar->text());
                emit entryTagRemoved(int(id), img, tag);
            });

    connect(m_entryPanel, &EntryPanel::entryListChanged, this,
            [this]() { m_entryView->query(m_searchBar->text()); });
    connect(m_entryPanel, &EntryPanel::entryModified, this, [this](int32_t) {
        m_entryView->query(m_searchBar->text());
        // LoRA path/strength edits flow through here too - keep the
        // active-stack cache in AppMainWindow in sync.
        m_entryView->refreshLoraStack();
    });

    QVBoxLayout* centerCol = new QVBoxLayout;
    centerCol->setContentsMargins(0, 0, 0, 0);
    centerCol->setSpacing(0);
    centerCol->addWidget(m_searchBar);
    centerCol->addWidget(m_entryView, 1);

    m_rootLayout = new QBoxLayout(QBoxLayout::LeftToRight, this);
    m_rootLayout->setContentsMargins(0, 0, 0, 0);
    m_rootLayout->setSpacing(0);
    m_rootLayout->addLayout(centerCol, 1);
    m_rootLayout->addWidget(m_entryPanel);

    m_entryPanel->setFixedWidth(480);
    m_entryView->query("");
}

void TileViewPage::setDanbooruIndex(core::DanbooruIndex* index)
{
    m_entryPanel->setDanbooruIndex(index);
}

void TileViewPage::setFacetIndex(core::FacetIndex* index)
{
    m_entryPanel->setFacetIndex(index);
}

void TileViewPage::refreshTags()
{
    m_entryPanel->refreshTags();
}

void TileViewPage::setActiveGroups(const QMap<int, QList<int>>& groups)
{
    m_entryPanel->setActiveGroups(groups);
    m_entryView->setActiveGroups(groups);
}

void TileViewPage::setLoraActiveByUuids(const QList<QString>& uuids)
{
    m_entryView->setLoraActiveByUuids(uuids);
}

QList<QString> TileViewPage::activeLoraUuids() const
{
    return m_entryView->activeLoraUuids();
}

void TileViewPage::setLoraDirs(const QString& primaryDir, const QString& testDir)
{
    m_entryPanel->setLoraDirs(primaryDir, testDir);
}

void TileViewPage::setLoraDefaults(double modelStr, double clipStr)
{
    m_entryPanel->setLoraDefaults(modelStr, clipStr);
}

void TileViewPage::setComfyClient(core::ComfyUiClient* client)
{
    m_entryPanel->setComfyClient(client);
}

void TileViewPage::setQuickFacets(const QString& characterFacet, const QString& copyrightFacet,
                                  const QString& triggerWordFacet, const QString& styleFacet)
{
    m_entryPanel->setQuickFacets(characterFacet, copyrightFacet, triggerWordFacet, styleFacet);
}

void TileViewPage::refreshEntries()
{
    m_entryView->query(m_searchBar->text());
}

void TileViewPage::selectEntry(int32_t entryId)
{
    m_entryView->selectAndScrollToEntry(entryId);
}

void TileViewPage::clearSearchAndSelect(int32_t entryId)
{
    // blockSignals + manual query so we don't bounce through the debounce
    // timer (would race with the immediate selectAndScroll call below).
    if (!m_searchBar->text().isEmpty()) {
        m_searchBar->blockSignals(true);
        m_searchBar->clear();
        m_searchBar->blockSignals(false);
        m_debounce->stop();
        m_entryView->query(QString());
    }
    m_entryView->selectAndScrollToEntry(entryId);
}

void TileViewPage::setTileGradient(qreal start, int alpha)
{
    m_entryView->setTileGradient(start, alpha);
}

void TileViewPage::setTileTitleColor(const QColor& color)
{
    m_entryView->setTileTitleColor(color);
}

} // namespace gui
