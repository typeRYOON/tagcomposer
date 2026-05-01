#include <gui/exportdialog.h>
#include <core/entrymodel.h>
#include <core/facetindex.h>
#include <core/portmanager.h>
#include <gui/widgets/appscrollbar.h>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

namespace gui {

ExportDialog::ExportDialog(core::EntryModel* model,
                           const core::FacetIndex* facets,
                           QWidget* parent)
    : QDialog(parent), m_model(model), m_facets(facets)
{
    setWindowTitle("Export Entries");
    setMinimumSize(720, 520);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);

    auto* prompt = new QLabel(
        "Enter a search query (same syntax as the tile-view bar). All\n"
        "matching entries and their referenced tag definitions will be exported.");
    prompt->setWordWrap(true);
    root->addWidget(prompt);

    m_query = new QLineEdit;
    m_query->setObjectName("SettingsInput");
    m_query->setPlaceholderText("e.g. \"kantai collection, -nsfw\"");
    root->addWidget(m_query);

    m_count = new QLabel("0 entries");
    root->addWidget(m_count);

    m_preview = new QListWidget;
    m_preview->setObjectName("WfFileList");
    m_preview->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));
    m_preview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_preview->setSelectionMode(QAbstractItemView::NoSelection);
    m_preview->setFocusPolicy(Qt::NoFocus);
    root->addWidget(m_preview, 1);

    m_status = new QLabel;
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Close);
    m_exportBtn = btns->addButton("Export to…", QDialogButtonBox::ActionRole);
    root->addWidget(btns);

    connect(m_query, &QLineEdit::textChanged, this, &ExportDialog::refreshPreview);
    connect(m_exportBtn, &QPushButton::clicked, this, &ExportDialog::onExport);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);

    refreshPreview();
}

void ExportDialog::refreshPreview()
{
    m_preview->clear();
    if (!m_model) {
        m_count->setText("entry model unavailable");
        m_exportBtn->setEnabled(false);
        return;
    }
    const auto matched = m_model->filter(m_query->text().trimmed());
    m_count->setText(QString("%1 entr%2 matched")
                         .arg(matched.size())
                         .arg(matched.size() == 1 ? "y" : "ies"));
    for (const auto* e : matched) {
        const QString display = e->title.isEmpty() ? e->uuid : e->title;
        m_preview->addItem(display);
    }
    m_exportBtn->setEnabled(!matched.isEmpty());
}

void ExportDialog::onExport()
{
    const QString folder = QFileDialog::getExistingDirectory(
        this, "Export to folder");
    if (folder.isEmpty()) return;

    if (!m_facets) {
        m_status->setText("Facet index unavailable — cannot export.");
        return;
    }

    QStringList errors;
    const bool ok = core::PortManager::exportEntries(
        m_query->text().trimmed(), folder, m_model, *m_facets, &errors);

    if (ok) {
        m_status->setText(QString("Exported successfully to: %1").arg(folder));
    } else {
        m_status->setText(QString("Export failed: %1").arg(errors.join("; ")));
    }
}

} // namespace gui
