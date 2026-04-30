#pragma once
#include <core/entry.h>
#include <core/entrymodel.h>
#include <core/workflowmanager.h>
#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QLabel>
#include <QFrame>

namespace gui {

class WorkflowEditPage : public QWidget {
    Q_OBJECT
public:
    explicit WorkflowEditPage(QWidget* parent = nullptr);

    void setWorkflowManager(core::WorkflowManager* wm, const QString& savePath);
    void setEntryModel(core::EntryModel* model);
    void refresh();

public slots:
    // Forwarded by AppMainWindow whenever the LoRA stack changes (tile view
    // toggles, session restore). Triggers a rebuild of the right-side panel.
    void setActiveLoraStack(const QList<core::LoraConfig>& stack);

signals:
    // Emitted after a LoRA strength edit so AppMainWindow can refresh its
    // cached active stack with the new values.
    void loraStrengthsChanged(const QList<core::LoraConfig>& stack);

private:
    void   rebuildVarList();
    QFrame* makeVarCard(int index);
    void   rebuildLoraList();
    QFrame* makeLoraCard(int index);
    void   addVariable(core::WorkflowVarType type);
    void   removeVariable(int index);
    void   save();

    core::WorkflowManager* m_wm        = nullptr;
    core::EntryModel*      m_entryModel = nullptr;
    QString                m_savePath;
    QList<core::LoraConfig> m_loraStack;

    QLabel*      m_titleLabel    = nullptr;
    QWidget*     m_varContainer  = nullptr;
    QVBoxLayout* m_varLayout     = nullptr;

    QLabel*      m_loraSubtitle  = nullptr;
    QWidget*     m_loraContainer = nullptr;
    QVBoxLayout* m_loraLayout    = nullptr;
};

} // namespace gui
