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
                           const QString& facetsSchemaPath, QWidget* parent)
    : ChromedDialog(parent), m_model(model), m_facets(facets), m_dataEntryDir(dataEntryDir),
      m_tagDefinitionsPath(tagDefinitionsPath), m_facetsSchemaPath(facetsSchemaPath)
{
    setWindowTitle("Import Entries");
    setMinimumSize(900, 720);

    auto* root = new QVBoxLayout(contentArea());
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(8);

    // ---- Source picker
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

    // ---- Summary
    m_summary = new QLabel("No source selected.");
    m_summary->setWordWrap(true);
    root->addWidget(m_summary);

    // ---- Conflict mode
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

    // ---- Mapping table header
    auto* mapHeader = new QHBoxLayout;
    mapHeader->addWidget(new QLabel("Facet mapping:"));
    mapHeader->addStretch();
    m_createAllBtn = new QPushButton("Create all unmatched");
    m_createAllBtn->setToolTip(
        "For every source facet not present locally, append it to facets.fct under\n"
        "@category Imported and use it as the mapping target.");
    m_createAllBtn->setCursor(Qt::PointingHandCursor);
    m_createAllBtn->setEnabled(false);
    mapHeader->addWidget(m_createAllBtn);
    m_dropAllBtn = new QPushButton("Drop all unmatched");
    m_dropAllBtn->setCursor(Qt::PointingHandCursor);
    m_dropAllBtn->setEnabled(false);
    mapHeader->addWidget(m_dropAllBtn);
    root->addLayout(mapHeader);

    // ---- Mapping table body
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

    // ---- Validation + status
    m_validation = new QLabel;
    m_validation->setObjectName("ValidationLabel");
    m_validation->setWordWrap(true);
    root->addWidget(m_validation);

    m_status = new QLabel;
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    // ---- Buttons
    auto* btns = new QDialogButtonBox(QDialogButtonBox::Cancel);
    m_importBtn = btns->addButton("Import", QDialogButtonBox::AcceptRole);
    m_importBtn->setEnabled(false);
    root->addWidget(btns);

    connect(browseBtn, &QPushButton::clicked, this, &ImportDialog::onBrowse);
    connect(m_dropAllBtn, &QPushButton::clicked, this, &ImportDialog::onDropAllUnmatched);
    connect(m_createAllBtn, &QPushButton::clicked, this, &ImportDialog::onCreateAllUnmatched);
    connect(btns, &QDialogButtonBox::accepted, this, &ImportDialog::onImport);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

// ---- Browse + scan

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
    m_createAllBtn->setEnabled(!m_scan.sourceFacets.isEmpty());
    rebuildMappingTable();
}

// ---- Mapping table

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

        auto* createBtn = new QPushButton("+");
        createBtn->setCheckable(true);
        createBtn->setFixedSize(28, 24);
        createBtn->setToolTip(
            "Create this facet in facets.fct under @category Imported and use it as\n"
            "the mapping target. The combo's current text is the new facet's name.");
        createBtn->setCursor(Qt::PointingHandCursor);
        rowLayout->addWidget(createBtn);

        auto* dropBtn = new QPushButton("x");
        dropBtn->setCheckable(true);
        dropBtn->setFixedSize(28, 24);
        dropBtn->setToolTip("Drop this facet - strip from any imported tag definition");
        dropBtn->setCursor(Qt::PointingHandCursor);
        rowLayout->addWidget(dropBtn);

        m_mappingLayout->insertWidget(m_mappingLayout->count() - 1, rowWidget);

        MappingRow rec;
        rec.source = src;
        rec.combo = combo;
        rec.createBtn = createBtn;
        rec.dropBtn = dropBtn;
        rec.widget = rowWidget;
        m_rows << rec;

        // Unmatched rows start in "needs decision" state so the user picks
        // between dropping, creating, or remapping; nothing silent.
        if (!inDest) {
            combo->setStyleSheet("background:#5a3a3a");
        }

        connect(combo, &QComboBox::currentTextChanged, this,
                [this](const QString&) { validate(); });
        connect(dropBtn, &QPushButton::toggled, this,
                [this, combo, createBtn](bool checked) {
                    if (checked && createBtn->isChecked()) createBtn->setChecked(false);
                    combo->setEnabled(!checked);
                    validate();
                });
        connect(createBtn, &QPushButton::toggled, this,
                [this, combo, dropBtn](bool checked) {
                    if (checked && dropBtn->isChecked()) dropBtn->setChecked(false);
                    combo->setEnabled(true);
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
    QStringList toDrop;
    QStringList toCreate;
    for (const auto& row : m_rows) {
        if (row.dropBtn->isChecked()) {
            toDrop << row.source;
            continue;
        }
        const QString target = row.combo->currentText().trimmed();
        if (row.createBtn->isChecked()) {
            if (target.isEmpty())
                unmapped << row.source;
            else
                toCreate << target;
            continue;
        }
        if (target.isEmpty() || !destSet.contains(target)) unmapped << row.source;
    }

    if (!unmapped.isEmpty()) {
        m_validation->setText(
            QString("%1 facet(s) need a decision (Drop, Create, or map to existing): %2")
                .arg(unmapped.size())
                .arg(unmapped.join(", ")));
        m_importBtn->setEnabled(false);
        return;
    }

    QStringList parts;
    if (!toCreate.isEmpty())
        parts << QString("Will create %1 new facet(s) under @category Imported: %2")
                     .arg(toCreate.size())
                     .arg(toCreate.join(", "));
    if (!toDrop.isEmpty())
        parts << QString("Will drop %1 facet(s) from imported defs: %2")
                     .arg(toDrop.size())
                     .arg(toDrop.join(", "));
    m_validation->setText(parts.join("\n"));
    m_importBtn->setEnabled(true);
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

void ImportDialog::onCreateAllUnmatched()
{
    if (!m_facets) return;
    const QList<QString> destFacets = m_facets->allFacets();
    QSet<QString> destSet(destFacets.begin(), destFacets.end());

    for (auto& row : m_rows) {
        if (!destSet.contains(row.combo->currentText().trimmed()))
            row.createBtn->setChecked(true);
    }
    validate();
}

// ---- Apply

void ImportDialog::onImport()
{
    if (!m_facets || !m_model) {
        reject();
        return;
    }

    core::PortConfig config;
    config.tagConflict = core::TagConflictMode(m_conflictGroup->checkedId());

    QStringList toCreate;
    for (const auto& row : m_rows) {
        if (row.dropBtn->isChecked()) {
            config.facetMapping[row.source] = QString();
            continue;
        }
        const QString target = row.combo->currentText().trimmed();
        if (row.createBtn->isChecked() && !target.isEmpty()) toCreate << target;
        config.facetMapping[row.source] = target;
    }

    if (!toCreate.isEmpty() && !m_facetsSchemaPath.isEmpty()) {
        m_facets->appendFacets(m_facetsSchemaPath, "Imported", toCreate);
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
