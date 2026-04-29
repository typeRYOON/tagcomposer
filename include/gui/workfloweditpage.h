#pragma once
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
    void refresh();

private:
    void   rebuildVarList();
    QFrame* makeVarCard(int index);
    void   addVariable(core::WorkflowVarType type);
    void   removeVariable(int index);
    void   save();

    core::WorkflowManager* m_wm       = nullptr;
    QString                m_savePath;

    QLabel*      m_titleLabel   = nullptr;
    QWidget*     m_varContainer = nullptr;
    QVBoxLayout* m_varLayout    = nullptr;
};

} // namespace gui
