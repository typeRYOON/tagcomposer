#pragma once
#include <gui/chromeddialog.h>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace core {
class DanbooruIndex;
class EntryModel;
class FacetIndex;
} // namespace core

namespace gui {

// Picks entries via the same query syntax as the tile-view search bar, then
// dumps them (plus a subset tag_definitions.fct) into a user-chosen folder.
class ExportDialog : public ChromedDialog {
    Q_OBJECT
public:
    ExportDialog(core::EntryModel* model, const core::FacetIndex* facets,
                 const core::DanbooruIndex* danbooru, QWidget* parent = nullptr);

private:
    void refreshPreview();
    void onExport();

    core::EntryModel* m_model = nullptr;
    const core::FacetIndex* m_facets = nullptr;
    const core::DanbooruIndex* m_danbooru = nullptr;

    QLineEdit* m_query = nullptr;
    QLabel* m_count = nullptr;
    QListWidget* m_preview = nullptr;
    QCheckBox* m_includeUnusedDefs = nullptr;
    QPushButton* m_exportBtn = nullptr;
    QLabel* m_status = nullptr;
};

} // namespace gui
