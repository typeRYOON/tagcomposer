#pragma once
#include <core/facetindex.h>
#include <core/entrymodel.h>
#include <QWidget>
#include <QListWidget>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QStackedWidget>
#include <functional>

namespace gui {

class FacetEditorPage : public QWidget {
    Q_OBJECT
public:
    explicit FacetEditorPage(
        core::FacetIndex*    facets,
        core::EntryModel*   model,
        QWidget*             parent = nullptr
    );

    void reload();

    // Source of the composer's currently-active tag list. Pulled on every
    // show of this page so the "undefined in composer" list stays fresh
    // without needing to plumb a signal through the composer.
    void setActiveTagsProvider(std::function<QList<QString>()> provider);

public slots:
    void selectTagByName(const QString& tag);
    void refreshUndefinedList();

signals:
    void facetsDefined();
    void wikiRequested(const QString& tag);
    // User clicked the schema reload button. Handled by AppMainWindow which
    // re-reads facets.fct into the shared FacetIndex and triggers reloadFacets.
    void schemaReloadRequested();

protected:
    void showEvent(QShowEvent* event) override;

private slots:
    void selectTag(const QString& tag);

private:
    void saveSelected();
    void clearEditor();
    void applyListFilter(const QString& query);
    void applyFacetFilter(const QString& query);

    core::FacetIndex*  m_facets;
    core::EntryModel* m_model;
    QString            m_selectedTag;

    std::function<QList<QString>()> m_activeTagsProvider;

    // Left panel
    QLineEdit*      m_searchEdit;
    QLabel*         m_undefinedHeader;
    QListWidget*    m_undefinedList;
    QListWidget*    m_tagList;
    QLabel*         m_countLabel;

    // Right panel
    QLabel*         m_selectedLabel;
    QLineEdit*      m_facetSearchEdit;
    QWidget*        m_facetsContainer;
    QVBoxLayout*    m_facetsLayout;
    QPushButton*    m_saveBtn;
    QStackedWidget* m_rightStack;
};

} // namespace gui
