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
    explicit LoraDropSearchBar(QWidget* parent = nullptr) : QLineEdit(parent) {
        setAcceptDrops(true);
    }
protected:
    void dragEnterEvent(QDragEnterEvent* e) override {
        if (!e->mimeData()->hasUrls()) return;
        const QString suffix = QFileInfo(e->mimeData()->urls().first().toLocalFile()).suffix().toLower();
        if (suffix == "safetensors") e->acceptProposedAction();
    }
    void dropEvent(QDropEvent* e) override {
        const QString path = e->mimeData()->urls().first().toLocalFile();
        if (path.isEmpty()) return;

        setEnabled(false);
        setPlaceholderText("hashing LoRA...");

        auto* watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcher<QString>::finished, this,
            [this, watcher]() {
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

TileViewPage::TileViewPage(core::EntryModel* model, QWidget* parent)
    : QWidget(parent)
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

    m_entryView  = new EntryView(model, this);
    m_entryPanel = new EntryPanel(model, this);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(150);
    connect(m_searchBar, &QLineEdit::textChanged, m_debounce, qOverload<>(&QTimer::start));
    connect(m_debounce, &QTimer::timeout, this, [this]() {
        m_entryView->query(m_searchBar->text());
    });

    // Entry click opens the panel
    connect(m_entryView, &EntryView::entryClicked, m_entryPanel, &EntryPanel::setEntry);

    // Auto-select + scroll-animate to a freshly-created entry. The view's
    // scrollToEntry already emits entryClicked, which routes to setEntry above.
    connect(m_entryPanel, &EntryPanel::entrySelectRequested,
            m_entryView, &EntryView::selectAndScrollToEntry);

    // Re-emit export, wiki, and facet-editor signals from panel and tile right-click
    connect(m_entryPanel, &EntryPanel::tagsExported, this, &TileViewPage::tagsExported);
    connect(m_entryView,  &EntryView::tagsExported,  this, &TileViewPage::tagsExported);
    connect(m_entryPanel, &EntryPanel::wikiRequested,           this, &TileViewPage::wikiRequested);
    connect(m_entryPanel, &EntryPanel::facetEditorRequested,    this, &TileViewPage::facetEditorRequested);
    connect(m_entryPanel, &EntryPanel::quickFacetRequested,     this, &TileViewPage::quickFacetRequested);
    connect(m_entryPanel, &EntryPanel::statusMessageRequested,  this, &TileViewPage::statusMessageRequested);

    // LoRA: forward stack changes from EntryView; clear from EntryPanel on lora removal
    connect(m_entryView, &EntryView::loraStackChanged, this, &TileViewPage::loraStackChanged);
    connect(m_entryPanel, &EntryPanel::loraCleared, m_entryView, &EntryView::clearLoraForEntry);

    // Model mutations from panel
    connect(m_entryPanel, &EntryPanel::entryTagAdded,
        this, [this, model](int32_t id, int img, const QString& tag) {
            model->addTagToImage(id, img, tag);
            m_entryView->query(m_searchBar->text());
            emit entryTagAdded(int(id), img, tag);
        });
    connect(m_entryPanel, &EntryPanel::entryTagRemoved,
        this, [this, model](int32_t id, int img, const QString& tag) {
            model->removeTagFromImage(id, img, tag);
            m_entryView->query(m_searchBar->text());
            emit entryTagRemoved(int(id), img, tag);
        });

    // View refresh signals
    connect(m_entryPanel, &EntryPanel::entryListChanged, this, [this]() {
        m_entryView->query(m_searchBar->text());
    });
    connect(m_entryPanel, &EntryPanel::entryModified, this, [this](int32_t) {
        m_entryView->query(m_searchBar->text());
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

    applyOrientation(false);
    m_entryView->query("");
}

void TileViewPage::setDanbooruIndex(core::DanbooruIndex* index)
{
    m_entryPanel->setDanbooruIndex(index);
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

void TileViewPage::setLoraBaseDir(const QString& dir)
{
    m_entryPanel->setLoraBaseDir(dir);
}

void TileViewPage::setComfyClient(core::ComfyUiClient* client)
{
    m_entryPanel->setComfyClient(client);
}

void TileViewPage::setQuickFacets(const QString& characterFacet,
                                  const QString& copyrightFacet,
                                  const QString& triggerWordFacet,
                                  const QString& styleFacet)
{
    m_entryPanel->setQuickFacets(characterFacet, copyrightFacet,
                                 triggerWordFacet, styleFacet);
}

void TileViewPage::refreshEntries()
{
    m_entryView->query(m_searchBar->text());
}

void TileViewPage::applyOrientation(bool portrait)
{
    m_rootLayout->setDirection(QBoxLayout::LeftToRight);
    m_entryPanel->setMaximumHeight(QWIDGETSIZE_MAX);
    m_entryPanel->setMinimumHeight(0);
    m_entryPanel->setFixedWidth(480);
    m_entryPanel->applyOrientation(portrait);
}

} // namespace gui
