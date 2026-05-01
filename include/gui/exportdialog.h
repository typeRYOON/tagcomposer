#pragma once
#include <QDialog>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace core {
class EntryModel;
class FacetIndex;
}

namespace gui {

// Picks entries via the same query syntax as the tile-view search bar, then
// dumps them (plus a subset tag_definitions.fct) into a user-chosen folder.
class ExportDialog : public QDialog {
    Q_OBJECT
public:
    ExportDialog(core::EntryModel* model,
                 const core::FacetIndex* facets,
                 QWidget* parent = nullptr);

private:
    void refreshPreview();
    void onExport();

    core::EntryModel*       m_model   = nullptr;
    const core::FacetIndex* m_facets  = nullptr;

    QLineEdit*   m_query    = nullptr;
    QLabel*      m_count    = nullptr;
    QListWidget* m_preview  = nullptr;
    QPushButton* m_exportBtn = nullptr;
    QLabel*      m_status   = nullptr;
};

} // namespace gui
