#pragma once
#include <QDialog>
#include <core/portmanager.h>

class QButtonGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace core {
class EntryModel;
class FacetIndex;
}

namespace gui {

// User-facing import flow. Picks an exported folder, scans it, lets the user
// remap source facets to local ones (or drop them) plus pick a tag-definition
// conflict mode, then applies via core::PortManager.
class ImportDialog : public QDialog {
    Q_OBJECT
public:
    ImportDialog(core::EntryModel* model,
                 core::FacetIndex* facets,
                 const QString& dataEntryDir,
                 const QString& tagDefinitionsPath,
                 QWidget* parent = nullptr);

private:
    struct MappingRow {
        QString      source;
        QComboBox*   combo  = nullptr;
        QPushButton* dropBtn = nullptr;
        QWidget*     widget = nullptr;
    };

    void onBrowse();
    void rebuildMappingTable();
    void validate();
    void onDropAllUnmatched();
    void onImport();

    core::EntryModel*  m_model    = nullptr;
    core::FacetIndex*  m_facets   = nullptr;
    QString            m_dataEntryDir;
    QString            m_tagDefinitionsPath;

    core::PortScan     m_scan;
    QList<MappingRow>  m_rows;

    QLineEdit*         m_srcInput        = nullptr;
    QLabel*            m_summary         = nullptr;
    QButtonGroup*      m_conflictGroup   = nullptr;
    QScrollArea*       m_mappingScroll   = nullptr;
    QWidget*           m_mappingHost     = nullptr;
    QVBoxLayout*       m_mappingLayout   = nullptr;
    QPushButton*       m_dropAllBtn      = nullptr;
    QPushButton*       m_importBtn       = nullptr;
    QLabel*            m_validation      = nullptr;
    QLabel*            m_status          = nullptr;
};

} // namespace gui
