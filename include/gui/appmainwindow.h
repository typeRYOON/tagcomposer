#pragma once
#include <model/entrymodel.h>
#include <core/danbooruindex.h>
#include <core/facetindex.h>
#include <core/ruleengine.h>
#include <core/taggroups.h>
#include <core/variableindex.h>
#include <core/promptpipeline.h>
#include <core/comfyuiclient.h>
#include <core/workflowmanager.h>
#include <utils/appsettings.h>
#include <QMainWindow>
#include <QCloseEvent>
#include <QStackedWidget>

namespace gui {
class TileViewPage;
class PromptComposerPage;
class FacetEditorPage;
class TagWikiPage;
class SettingsPage;
class WorkflowEditPage;
class DatasetHelpersPage;
class StatusBar;
class DanmakuOverlay;

class AppMainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit AppMainWindow(QWidget* parent = nullptr);
    ~AppMainWindow() = default;

    AppMainWindow(const AppMainWindow&)            = delete;
    AppMainWindow& operator=(const AppMainWindow&) = delete;
    AppMainWindow(AppMainWindow&&)                 = delete;
    AppMainWindow& operator=(AppMainWindow&&)      = delete;

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void reloadFacets();
    void applyComfySettings();

    model::EntryModel*   m_entryModel;
    QStackedWidget*      m_pages;
    bool                 m_isFullScreen{ false };

    gui::TileViewPage*        m_tileViewPage      = nullptr;
    gui::PromptComposerPage*  m_composerPage      = nullptr;
    gui::FacetEditorPage*     m_facetEditorPage   = nullptr;
    gui::TagWikiPage*         m_wikiPage          = nullptr;
    gui::SettingsPage*        m_settingsPage      = nullptr;
    gui::WorkflowEditPage*    m_workflowEditPage  = nullptr;
    gui::StatusBar*           m_statusBar         = nullptr;
    gui::DanmakuOverlay*      m_danmakuOverlay    = nullptr;

    core::DanbooruIndex* m_danbooruIndex = nullptr;
    core::ComfyUiClient* m_comfyClient   = nullptr;

    utils::AppSettings m_settings;

    QList<core::LoraConfig> m_activeLoraStack;
    QList<QString>          m_activeLoraUuids;

    // Pipeline stack (value types stored here; pipeline holds pointers to them)
    core::FacetIndex      m_facetIndex;
    core::RuleEngine      m_ruleEngine;
    core::TagGroupIndex   m_tagGroupIndex;
    core::VariableIndex   m_varIndex;
    core::WorkflowManager m_workflowManager;
    core::PromptPipeline* m_pipeline = nullptr;
};

}
