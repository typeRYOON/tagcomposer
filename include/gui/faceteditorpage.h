#pragma once
#include <core/facetindex.h>
#include <model/entrymodel.h>
#include <QWidget>
#include <QListWidget>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QCheckBox>
#include <QStackedWidget>

namespace gui {

class FacetEditorPage : public QWidget {
    Q_OBJECT
public:
    explicit FacetEditorPage(
        core::FacetIndex*    facets,
        model::EntryModel*   model,
        QWidget*             parent = nullptr
    );

    void reload();

public slots:
    void selectTagByName(const QString& tag);

signals:
    void facetsDefined();

private slots:
    void selectTag(const QString& tag);

private:
    void saveSelected();
    void clearEditor();

    core::FacetIndex*  m_facets;
    model::EntryModel* m_model;
    QString            m_selectedTag;

    // Left panel
    QLineEdit*      m_searchEdit;
    QListWidget*    m_tagList;
    QLabel*         m_countLabel;

    // Right panel
    QLabel*         m_selectedLabel;
    QWidget*        m_facetsContainer;
    QVBoxLayout*    m_facetsLayout;
    QPushButton*    m_saveBtn;
    QStackedWidget* m_rightStack;
};

} // namespace gui
