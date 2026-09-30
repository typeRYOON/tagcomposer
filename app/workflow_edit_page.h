#pragma once
#include <core/entry.h>
#include <core/workflow.h>
#include <QList>
#include <QWidget>

class QFrame;
class QLabel;
class QListWidget;
class QPushButton;
class QTimer;
class QVBoxLayout;

namespace tc {

class AppData;
class EntryStore;
class EntrySearch;
class WorkflowInputCache;

// Three columns: the selected workflow's variables, the active LoRA stack,
// and the batch runner.
//
// Every edit writes through to workflows.json immediately, which is what the
// old page did - there is no apply step to forget.
class WorkflowEditPage : public QWidget {
    Q_OBJECT

public:
    WorkflowEditPage(AppData& data, EntryStore& entries, EntrySearch& search,
                     WorkflowInputCache& cache, QWidget* parent = nullptr);

    // Re-reads the selected workflow: its name and its variable cards.
    void refresh();

public slots:
    // The shell owns the active stack; this mirrors it.
    void setActiveLoraStack(const QList<Lora>& stack);
    void setBatchResult(const QString& message);

signals:
    void loraStrengthsChanged(const QList<Lora>& stack);
    void batchRunRequested(const QString& query);
    void statusMessage(const QString& message);

private:
    void rebuildVarList();
    QFrame* makeVarCard(int index);
    void rebuildLoraList();
    QFrame* makeLoraCard(int index);

    void addVariable(const WorkflowVarValue& value, const QString& placeholder);
    void removeVariable(int index);

    // Nothing here is valid until a workflow is picked in the composer.
    Workflow* selectedWorkflow() const;
    QList<WorkflowVar>* variables() const;
    void save();

    AppData* m_data = nullptr;
    EntryStore* m_entries = nullptr;
    EntrySearch* m_search = nullptr;
    WorkflowInputCache* m_cache = nullptr;

    QList<Lora> m_loraStack;

    QLabel* m_titleLabel = nullptr;
    QPushButton* m_addBtn = nullptr;
    QVBoxLayout* m_varLayout = nullptr;

    QLabel* m_loraSubtitle = nullptr;
    QVBoxLayout* m_loraLayout = nullptr;

    QLabel* m_batchStatus = nullptr;
    QLabel* m_batchCountLabel = nullptr;
    QListWidget* m_batchResultsList = nullptr;
    QTimer* m_batchQueryDebounce = nullptr;
};

} // namespace tc
