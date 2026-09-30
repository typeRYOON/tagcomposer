#pragma once
#include <core/rule.h>
#include <core/variables.h>
#include <core/workflow.h>
#include <QWidget>

class QListWidget;
class QVBoxLayout;

namespace tc {

// The composer's right column: RULES, WORKFLOWS and VARIABLES stacked in a
// draggable splitter, matching the old sidebar's sections and styling.
//
// It renders what it is given and emits intent. Applying a rule toggle or a
// variable edit is the page's job, because those write back to disk.
class ComposerSidebar : public QWidget {
    Q_OBJECT

public:
    explicit ComposerSidebar(QWidget* parent = nullptr);

    void setRules(const QList<Rule>& rules);
    void setWorkflows(const QList<Workflow>& workflows, int selected);
    void setVariables(const QList<Variable>& variables);

    int selectedWorkflow() const;

signals:
    void ruleToggled(const QString& uuid, bool enabled);
    void workflowSelected(int index);

    void variableChanged(const QString& name, const QString& value);
    void variableAdded(const QString& name, const QString& value);
    void variableRemoved(const QString& name);

    // Header buttons: the page owns the files, so it does the reloading.
    void reloadRulesRequested();
    void reloadVariablesRequested();
    void openRulesFileRequested();
    void openVariablesFileRequested();
    void workflowEditorRequested();

    void runRequested();
    void interruptRequested();
    void clearQueueRequested();

private:
    QWidget* makeSection(const QString& title, QWidget* body,
                         const QList<QWidget*>& headerWidgets);

    QVBoxLayout* m_ruleRows = nullptr;
    QVBoxLayout* m_variableRows = nullptr;
    QListWidget* m_workflowList = nullptr;
};

} // namespace tc
