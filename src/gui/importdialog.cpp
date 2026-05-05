#include <gui/importdialog.h>
#include <core/entrymodel.h>
#include <core/facetindex.h>
#include <core/portmanager.h>
#include <gui/widgets/appscrollbar.h>
#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSet>
#include <QVBoxLayout>

namespace gui {

ImportDialog::ImportDialog(core::EntryModel* model, core::FacetIndex* facets,
                           const QString& dataEntryDir, const QString& tagDefinitionsPath,
                           QWidget* parent)
    : ChromedDialog(parent), m_model(model), m_facets(facets), m_dataEntryDir(dataEntryDir),
      m_tagDefinitionsPath(tagDefinitionsPath)
{
    setWindowTitle("Import Entries");
    setMinimumSize(900, 720);

    auto* root = new QVBoxLayout(contentArea());
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(8);

    // ── Source picker ───────────────────────────────────────────────────────
    auto* srcRow = new QHBoxLayout;
    auto* srcLabel = new QLabel("Source:");
    m_srcInput = new QLineEdit;
    m_srcInput->setObjectName("SettingsInput");
    m_srcInput->setPlaceholderText("Choose a previously-exported folder…");
    m_srcInput->setReadOnly(true);
    auto* browseBtn = new QPushButton("Browse…");
    browseBtn->setCursor(Qt::PointingHandCursor);
    srcRow->addWidget(srcLabel);
    srcRow->addWidget(m_srcInput, 1);
    srcRow->addWidget(browseBtn);
    root->addLayout(srcRow);

    // ── Summary ─────────────────────────────────────────────────────────────
    m_summary = new QLabel("No source selected.");
    m_summary->setWordWrap(true);
    root->addWidget(m_summary);

    // ── Conflict mode ───────────────────────────────────────────────────────
    auto* conflictRow = new QHBoxLayout;
    conflictRow->addWidget(new QLabel("On tag-definition collision:"));
    auto* skipRb = new QRadioButton("Skip");
    auto* mergeRb = new QRadioButton("Merge");
    auto* overwriteRb = new QRadioButton("Overwrite");
    skipRb->setChecked(true);
    m_conflictGroup = new QButtonGroup(this);
    m_conflictGroup->addButton(skipRb, int(core::TagConflictMode::Skip));
    m_conflictGroup->addButton(mergeRb, int(core::TagConflictMode::Merge));
    m_conflictGroup->addButton(overwriteRb, int(core::TagConflictMode::Overwrite));
    conflictRow->addWidget(skipRb);
    conflictRow->addWidget(mergeRb);
    conflictRow->addWidget(overwriteRb);
    conflictRow->addStretch();
    root->addLayout(conflictRow);

    // ── Mapping table header ────────────────────────────────────────────────
    auto* mapHeader = new QHBoxLayout;
    mapHeader->addWidget(new QLabel("Facet mapping:"));
    mapHeader->addStretch();
    m_dropAllBtn = new QPushButton("Drop all unmatched");
    m_dropAllBtn->setCursor(Qt::PointingHandCursor);
    m_dropAllBtn->setEnabled(false);
    mapHeader->addWidget(m_dropAllBtn);
    root->addLayout(mapHeader);

    // ── Mapping table body ──────────────────────────────────────────────────
    m_mappingHost = new QWidget;
    m_mappingLayout = new QVBoxLayout(m_mappingHost);
    m_mappingLayout->setContentsMargins(0, 0, 0, 0);
    m_mappingLayout->setSpacing(4);
    m_mappingLayout->addStretch();

    m_mappingScroll = new QScrollArea;
    m_mappingScroll->setWidget(m_mappingHost);
    m_mappingScroll->setWidgetResizable(true);
    m_mappingScroll->setFrameShape(QFrame::NoFrame);
    m_mappingScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_mappingScroll->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));
    root->addWidget(m_mappingScroll, 1);

    // ── Validation + status ─────────────────────────────────────────────────
    m_validation = new QLabel;
    m_validation->setObjectName("ValidationLabel");
    m_validation->setWordWrap(true);
    root->addWidget(m_validation);

    m_status = new QLabel;
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    // ── Buttons ─────────────────────────────────────────────────────────────
    auto* btns = new QDialogButtonBox(QDialogButtonBox::Cancel);
    m_importBtn = btns->addButton("Import", QDialogButtonBox::AcceptRole);
    m_importBtn->setEnabled(false);
    root->addWidget(btns);

    connect(browseBtn, &QPushButton::clicked, this, &ImportDialog::onBrowse);
    connect(m_dropAllBtn, &QPushButton::clicked, this, &ImportDialog::onDropAllUnmatched);
    connect(btns, &QDialogButtonBox::accepted, this, &ImportDialog::onImport);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

// ── Browse + scan ────────────────────────────────────────────────────────────

void ImportDialog::onBrowse()
{
    const QString folder = QFileDialog::getExistingDirectory(this, "Import from folder");
    if (folder.isEmpty()) return;

    m_srcInput->setText(folder);
    m_status->clear();

    if (!m_facets || !m_model) {
        m_summary->setText("EntryModel or FacetIndex unavailable.");
        return;
    }

    m_scan = core::PortManager::scanImport(folder, m_model, *m_facets);

    int dupes = 0;
    for (const auto& e : m_scan.entries)
        if (e.duplicate) ++dupes;
    int collisions = 0;
    for (const auto& d : m_scan.tagDefs)
        if (d.collision) ++collisions;

    m_summary->setText(QString("Entries: %1 (%2 duplicate(s) will be skipped) · "
                               "Tag definitions: %3 (%4 collision(s)) · "
                               "Source facets: %5")
                           .arg(m_scan.entries.size())
                           .arg(dupes)
                           .arg(m_scan.tagDefs.size())
                           .arg(collisions)
                           .arg(m_scan.sourceFacets.size()));

    m_dropAllBtn->setEnabled(!m_scan.sourceFacets.isEmpty());
    rebuildMappingTable();
}

// ── Mapping table ────────────────────────────────────────────────────────────

void ImportDialog::rebuildMappingTable()
{
    // Drop existing rows (keep the trailing stretch)
    for (auto& row : m_rows) {
        if (row.widget) row.widget->deleteLater();
    }
    m_rows.clear();

    if (!m_facets) {
        validate();
        return;
    }
    const QList<QString> destFacets = m_facets->allFacets();
    QSet<QString> destSet(destFacets.begin(), destFacets.end());

    for (const QString& src : m_scan.sourceFacets) {
        const bool inDest = destSet.contains(src);

        auto* rowWidget = new QWidget;
        auto* rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);

        auto* srcLabel = new QLabel(src);
        srcLabel->setMinimumWidth(180);
        rowLayout->addWidget(srcLabel);

        auto* arrow = new QLabel("→");
        rowLayout->addWidget(arrow);

        auto* combo = new QComboBox;
        combo->setEditable(true);
        combo->addItems(destFacets);
        combo->setCurrentText(src);
        rowLayout->addWidget(combo, 1);

        auto* dropBtn = new QPushButton("✕");
        dropBtn->setCheckable(true);
        dropBtn->setFixedSize(28, 24);
        dropBtn->setToolTip("Drop this facet - strip from any imported tag definition");
        dropBtn->setCursor(Qt::PointingHandCursor);
        rowLayout->addWidget(dropBtn);

        m_mappingLayout->insertWidget(m_mappingLayout->count() - 1, rowWidget);

        MappingRow rec;
        rec.source = src;
        rec.combo = combo;
        rec.dropBtn = dropBtn;
        rec.widget = rowWidget;
        m_rows << rec;

        // Default-drop rows whose source name isn't in the destination schema
        if (!inDest) dropBtn->setChecked(true);

        connect(combo, &QComboBox::currentTextChanged, this,
                [this](const QString&) { validate(); });
        connect(dropBtn, &QPushButton::toggled, this, [this, combo](bool checked) {
            combo->setEnabled(!checked);
            validate();
        });
    }

    validate();
}

void ImportDialog::validate()
{
    if (!m_facets) {
        m_validation->setText("Facet index unavailable");
        m_importBtn->setEnabled(false);
        return;
    }

    // Disable Import when there's nothing to do; Cancel still works.
    if (m_scan.entries.isEmpty() && m_scan.tagDefs.isEmpty()) {
        m_validation->clear();
        m_importBtn->setEnabled(false);
        return;
    }

    const QList<QString> destFacets = m_facets->allFacets();
    QSet<QString> destSet(destFacets.begin(), destFacets.end());

    QStringList unmapped;
    for (const auto& row : m_rows) {
        if (row.dropBtn->isChecked()) continue;
        const QString target = row.combo->currentText().trimmed();
        if (target.isEmpty() || !destSet.contains(target)) unmapped << row.source;
    }

    if (unmapped.isEmpty()) {
        m_validation->clear();
        m_importBtn->setEnabled(true);
    }
    else {
        m_validation->setText(QString("%1 facet(s) need a valid destination or to be dropped: %2")
                                  .arg(unmapped.size())
                                  .arg(unmapped.join(", ")));
        m_importBtn->setEnabled(false);
    }
}

void ImportDialog::onDropAllUnmatched()
{
    if (!m_facets) return;
    const QList<QString> destFacets = m_facets->allFacets();
    QSet<QString> destSet(destFacets.begin(), destFacets.end());

    for (auto& row : m_rows) {
        if (!destSet.contains(row.combo->currentText().trimmed())) row.dropBtn->setChecked(true);
    }
    validate();
}

// ── Apply ────────────────────────────────────────────────────────────────────

void ImportDialog::onImport()
{
    if (!m_facets || !m_model) {
        reject();
        return;
    }

    core::PortConfig config;
    config.tagConflict = core::TagConflictMode(m_conflictGroup->checkedId());

    for (const auto& row : m_rows) {
        if (row.dropBtn->isChecked())
            config.facetMapping[row.source] = QString(); // explicit drop
        else
            config.facetMapping[row.source] = row.combo->currentText().trimmed();
    }

    const core::PortResult res = core::PortManager::applyImport(
        m_scan, config, m_model, *m_facets, m_dataEntryDir, m_tagDefinitionsPath);

    QString summary = QString("Entries imported: %1 · skipped (duplicates): %2\n"
                              "Tag definitions - added: %3 · merged: %4 · overwritten: %5 · "
                              "skipped: %6 · dropped (empty after mapping): %7")
                          .arg(res.entriesImported)
                          .arg(res.entriesSkipped)
                          .arg(res.tagsAdded)
                          .arg(res.tagsMerged)
                          .arg(res.tagsOverwritten)
                          .arg(res.tagsSkipped)
                          .arg(res.tagsDroppedEmpty);

    if (!res.errors.isEmpty()) summary += "\nErrors: " + res.errors.join("; ");

    m_status->setText(summary);
    accept();
}

} // namespace gui
