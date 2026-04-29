#pragma once
#include <core/facetindex.h>
#include <core/ruleengine.h>
#include <core/savedstate.h>
#include <core/taggroups.h>
#include <core/variableindex.h>
#include <core/promptpipeline.h>
#include <core/danbooruindex.h>
#include <core/workflowmanager.h>
#include <gui/tagsearchbar.h>
#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QSet>
#include <QMap>
#include <QHash>
#include <QStackedWidget>
#include <QStackedLayout>
#include <QPushButton>
#include <QLabel>
#include <QImage>
#include <QPixmap>
#include <QResizeEvent>
#include <QShowEvent>
#include <QMouseEvent>
#include <QSpinBox>
#include <QListWidget>
#include <QListWidgetItem>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;

namespace model { class EntryModel; }

namespace gui {


class ComposerScrollArea : public QScrollArea {
    Q_OBJECT
public:
    explicit ComposerScrollArea(QWidget* parent = nullptr);

signals:
    void runRequested();
    void interruptRequested();
    void clearPendingRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;
};





class WorkflowDropList : public QListWidget {
    Q_OBJECT
public:
    explicit WorkflowDropList(QWidget* parent = nullptr);
signals:
    void fileDropped(const QString& path);
protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
};

class StatesListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit StatesListWidget(QWidget* parent = nullptr);
signals:
    void imageDroppedOnRow(int row, const QString& imagePath);
protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
private:
    static bool isImagePath(const QString& path);
};

// QLabel that supports hover QSS, click signal, and an in-frame step overlay
class PreviewClickLabel : public QLabel {
    Q_OBJECT
public:
    explicit PreviewClickLabel(QWidget* parent = nullptr);
    void setStepText(const QString& text); // empty = hide overlay
signals:
    void clicked();
protected:
    void mousePressEvent(QMouseEvent* e) override;
    void paintEvent(QPaintEvent* e) override;
private:
    QString m_stepText;
};

class PromptComposerPage : public QWidget {
    Q_OBJECT
public:
    explicit PromptComposerPage(
        core::PromptPipeline*      pipeline,
        core::RuleEngine*          rules,
        const core::TagGroupIndex& groups,
        QWidget*                   parent = nullptr
    );

    void setDanbooruIndex(core::DanbooruIndex* index);
    void setVariableIndex(core::VariableIndex* index);
    void setWorkflowManager(core::WorkflowManager* wm, const QString& savePath);
    void setStatesDir(const QString& dir);
    void setEntryModel(model::EntryModel* model);

    QString currentPromptString(bool forJson) const;

    void saveSession(const QString& path) const;
    void restoreSession(const QString& path);

public slots:
    void triggerRun();
    void loadPipeline(int entryId, int imageIdx, const QList<QString>& tags);
    void onPipelineReady(QList<core::CategoryGroup> groups);
    void onEntryTagAdded(int entryId, int imageIdx, const QString& tag);
    void onEntryTagRemoved(int entryId, int imageIdx, const QString& tag);
    void setPreviewImage(const QImage& image);
    void setQueueCount(int count);
    void setPreviewProgress(int step, int total);
    void setOutputFolderPattern(const QString& pattern);
    void setTempFolder(const QString& folder);
    void setActiveLoraUuids(const QList<QString>& uuids);
    void repush();

signals:
    void activeGroupsChanged(QMap<int, QList<int>> activeGroups);
    void wikiRequested(const QString& tag);
    void facetEditorRequested(const QString& tag);
    void runRequested(int count);
    void interruptRequested();
    void clearPendingRequested();
    void workflowEditorRequested();
    void workflowVarsChanged();
    void statusMessageRequested(const QString& message);
    void loraUuidsRestored(QList<QString> uuids);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void rebuildGroupsDisplay(const QList<core::PipelineTag>& flat);
    void applyTagFilter();
    void rebuildRulesSidebar();
    void rebuildVarsSidebar();
    void rebuildWorkflowList();
    void rebuildStatesList();
    void repositionFloats();
    void reloadRules();
    void reloadVars();
    void saveCurrentState();
    void restoreState(const core::SavedState& state);
    void showStatePreview(int row);
    void hideStatePreview();

    QWidget* makeTagRow(const core::PipelineTag& pt);

    core::PromptPipeline*  m_pipeline;
    core::RuleEngine*      m_rules;
    core::TagGroupIndex    m_groups;
    core::VariableIndex*   m_varIndex   = nullptr;
    core::WorkflowManager* m_wfManager  = nullptr;
    model::EntryModel*     m_entryModel = nullptr;
    QString                m_wfSavePath;

    QString                  m_filterQuery;
    QList<QString>           m_activeLoraUuids;
    QList<QString>           m_activeTags;
    QSet<QString>            m_activeTagSet;
    QSet<QString>            m_deactivatedTags; // tags kept in list but excluded from pipeline
    QList<core::PipelineTag> m_lastResult;
    QList<core::CategoryGroup> m_lastGroups;  // categorized, weights applied
    QHash<QString, float>    m_tagWeights;    // user-set weights keyed by tag text

    QHash<qint64, QList<QString>> m_activePushes;

    // UI — main area
    TagSearchBar*      m_searchBar;
    QStackedWidget*    m_mainStack;
    QWidget*           m_groupsContainer;
    QVBoxLayout*       m_groupsLayout;
    QWidget*           m_centerBg;

    // Floating preview widgets (bottom-right, above control bar)
    PreviewClickLabel* m_previewLabel;  // floating preview image (step text drawn inside it)
    QWidget*           m_controlBar;    // floating control bar below preview
    QPushButton*       m_runBtn;
    QSpinBox*          m_promptCountSpin;
    QPushButton*       m_interruptBtn;
    QLabel*            m_queueLabel;

    // Preview popout window (created on first click, Qt::Window)
    QWidget*           m_popout = nullptr;
    QPixmap            m_currentPix;
    QString            m_outputFolderPattern;
    QString            m_tempFolder;

    // UI — workflow/states sidebar
    WorkflowDropList*  m_wfList        = nullptr;
    StatesListWidget*  m_statesList    = nullptr;
    QStackedWidget*    m_wfStateStack  = nullptr;
    QPushButton*       m_wfEditBtnRef  = nullptr;
    QPushButton*       m_saveStateBtn  = nullptr;
    QLabel*            m_statesPreviewPopup = nullptr;

    // State management
    core::StateManager m_stateManager;
    QString            m_statesDir;
    bool               m_suppressRuleSave   = false;
    bool               m_freezeNextRebuild  = false;

    // UI — rule sidebar
    QWidget*     m_rulesContainer;
    QVBoxLayout* m_rulesLayout;

    // UI — variable sidebar section
    QWidget*     m_varsContainer;
    QVBoxLayout* m_varsLayout;

    // Copy-to-clipboard button area
    QPushButton* m_copyBtn;

    // Category nav panel (top-right float)
    QWidget*     m_categoryNav  = nullptr;
    QPushButton* m_clearBtn     = nullptr;
    ComposerScrollArea* m_groupsScroll = nullptr;
    QMap<QString, QWidget*> m_groupHeaders; // display name → header label
};

} // namespace gui
