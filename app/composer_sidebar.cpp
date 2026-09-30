#include <app/composer_sidebar.h>
#include <app/icons.h>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

QPushButton* sidebarButton(const QIcon& icon, const QString& tooltip)
{
    auto* button = new QPushButton;
    button->setObjectName(u"SidebarBtn"_s);
    button->setFixedSize(20, 20);
    button->setIcon(icon);
    button->setIconSize(QSize(14, 14));
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(tooltip);
    return button;
}

// Keeps the trailing stretch.
void clearRows(QVBoxLayout* layout)
{
    while (layout->count() > 1) {
        QLayoutItem* item = layout->takeAt(0);
        if (QWidget* widget = item->widget()) delete widget;
        delete item;
    }
}

} // namespace

ComposerSidebar::ComposerSidebar(QWidget* parent) : QWidget(parent)
{
    setObjectName(u"ComposerSidebar"_s);
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(330);

    // ---- RULES
    auto* ruleContainer = new QWidget;
    ruleContainer->setObjectName(u"ComposerRulesContainer"_s);
    m_ruleRows = new QVBoxLayout(ruleContainer);
    m_ruleRows->setContentsMargins(8, 8, 8, 8);
    m_ruleRows->setSpacing(4);
    m_ruleRows->addStretch();

    auto* ruleScroll = new QScrollArea;
    ruleScroll->setObjectName(u"ComposerRulesScroll"_s);
    ruleScroll->setWidget(ruleContainer);
    ruleScroll->setWidgetResizable(true);
    ruleScroll->setFrameShape(QFrame::NoFrame);
    ruleScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QPushButton* rulesOpen = sidebarButton(icons::openExternal(), u"Open rules.fct in editor"_s);
    QPushButton* rulesReload = sidebarButton(icons::reload(), u"Reload rules from file"_s);
    auto* rulesSection = makeSection(u"RULES"_s, ruleScroll, {rulesOpen, rulesReload});

    // ---- WORKFLOWS
    m_workflowList = new QListWidget;
    m_workflowList->setObjectName(u"WfList"_s);
    m_workflowList->setFrameShape(QFrame::NoFrame);
    m_workflowList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* run = new QPushButton(u"Run"_s);
    run->setObjectName(u"ComposerRunBtn"_s);
    auto* stop = new QPushButton(u"Stop"_s);
    stop->setObjectName(u"ComposerInterruptBtn"_s);
    auto* clear = new QPushButton(u"Clear"_s);
    clear->setObjectName(u"ComposerCopyBtn"_s);

    auto* runRow = new QHBoxLayout;
    runRow->setContentsMargins(8, 4, 8, 8);
    runRow->setSpacing(4);
    runRow->addWidget(run, 1);
    runRow->addWidget(stop);
    runRow->addWidget(clear);

    auto* workflowBody = new QWidget;
    auto* workflowLayout = new QVBoxLayout(workflowBody);
    workflowLayout->setContentsMargins(0, 0, 0, 0);
    workflowLayout->setSpacing(0);
    workflowLayout->addWidget(m_workflowList, 1);
    workflowLayout->addLayout(runRow);

    QPushButton* workflowEdit =
        sidebarButton(icons::openExternal(), u"Workflow Variable Editor"_s);
    auto* workflowSection = makeSection(u"WORKFLOWS"_s, workflowBody, {workflowEdit});

    // ---- VARIABLES
    auto* variableContainer = new QWidget;
    variableContainer->setObjectName(u"ComposerVarsContainer"_s);
    m_variableRows = new QVBoxLayout(variableContainer);
    m_variableRows->setContentsMargins(8, 6, 8, 8);
    m_variableRows->setSpacing(6);
    m_variableRows->addStretch();

    auto* variableScroll = new QScrollArea;
    variableScroll->setObjectName(u"ComposerVarsScroll"_s);
    variableScroll->setWidget(variableContainer);
    variableScroll->setWidgetResizable(true);
    variableScroll->setFrameShape(QFrame::NoFrame);
    variableScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QPushButton* varsOpen = sidebarButton(icons::openExternal(), u"Open vars.fct in editor"_s);
    QPushButton* varsReload = sidebarButton(icons::reload(), u"Reload variables from file"_s);
    auto* variablesSection = makeSection(u"VARIABLES"_s, variableScroll, {varsOpen, varsReload});

    // Rules takes the leftover space.
    auto* split = new QSplitter(Qt::Vertical);
    split->setObjectName(u"ComposerSidebarSplit"_s);
    split->setHandleWidth(5);
    split->setChildrenCollapsible(false);
    split->addWidget(rulesSection);
    split->addWidget(workflowSection);
    split->addWidget(variablesSection);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);
    split->setStretchFactor(2, 0);
    split->setSizes({400, 240, 200});

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(split, 1);

    connect(run, &QPushButton::clicked, this, &ComposerSidebar::runRequested);
    connect(stop, &QPushButton::clicked, this, &ComposerSidebar::interruptRequested);
    connect(clear, &QPushButton::clicked, this, &ComposerSidebar::clearQueueRequested);
    connect(rulesOpen, &QPushButton::clicked, this, &ComposerSidebar::openRulesFileRequested);
    connect(rulesReload, &QPushButton::clicked, this, &ComposerSidebar::reloadRulesRequested);
    connect(varsOpen, &QPushButton::clicked, this, &ComposerSidebar::openVariablesFileRequested);
    connect(varsReload, &QPushButton::clicked, this, &ComposerSidebar::reloadVariablesRequested);
    connect(workflowEdit, &QPushButton::clicked, this,
            &ComposerSidebar::workflowEditorRequested);
    connect(m_workflowList, &QListWidget::currentRowChanged, this,
            &ComposerSidebar::workflowSelected);
}

QWidget* ComposerSidebar::makeSection(const QString& title, QWidget* body,
                                      const QList<QWidget*>& headerWidgets)
{
    auto* header = new QWidget;
    header->setObjectName(u"ComposerHeaderRow"_s);
    header->setAttribute(Qt::WA_StyledBackground, true);

    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(12, 8, 8, 6);
    headerLayout->setSpacing(4);

    auto* label = new QLabel(title);
    label->setObjectName(u"ComposerHeaderLabel"_s);
    headerLayout->addWidget(label, 1);

    for (QWidget* widget : headerWidgets)
        headerLayout->addWidget(widget);

    auto* section = new QWidget;
    auto* layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
    layout->addWidget(body, 1);
    return section;
}

void ComposerSidebar::setRules(const QList<Rule>& rules)
{
    clearRows(m_ruleRows);

    for (const Rule& rule : rules) {
        auto* row = new QWidget;
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);

        auto* toggle = new QCheckBox;
        toggle->setObjectName(u"ComposerRuleToggle"_s);
        toggle->setChecked(rule.enabled);
        layout->addWidget(toggle);

        auto* name = new QLabel(rule.name);
        name->setObjectName(u"ComposerRuleName"_s);
        layout->addWidget(name, 1);

        if (rule.force) {
            auto* badge = new QLabel(u"F"_s);
            badge->setObjectName(u"ComposerRuleForceBadge"_s);
            badge->setToolTip(u"Fires even when nothing matched"_s);
            layout->addWidget(badge);
        }

        const bool injects = rule.action.type == ActionType::Add
            || rule.action.type == ActionType::Replace;
        if (injects) {
            auto* badge = new QLabel(rule.action.type == ActionType::Add ? u"+"_s : u"~"_s);
            badge->setObjectName(rule.action.type == ActionType::Add
                                     ? u"ComposerRuleAddBadge"_s
                                     : u"ComposerRuleReplaceBadge"_s);
            badge->setToolTip(rule.action.arguments.join(u", "_s));
            layout->addWidget(badge);
        }

        const QString uuid = rule.uuid;
        connect(toggle, &QCheckBox::toggled, this,
                [this, uuid](bool on) { emit ruleToggled(uuid, on); });

        m_ruleRows->insertWidget(m_ruleRows->count() - 1, row);
    }

    if (rules.isEmpty()) {
        auto* hint = new QLabel(u"No rules loaded"_s);
        hint->setObjectName(u"ComposerRulesHint"_s);
        m_ruleRows->insertWidget(0, hint);
    }
}

void ComposerSidebar::setWorkflows(const QList<Workflow>& workflows, int selected)
{
    const QSignalBlocker block(m_workflowList);
    m_workflowList->clear();

    for (const Workflow& workflow : workflows)
        m_workflowList->addItem(workflow.name);

    if (selected >= 0 && selected < m_workflowList->count())
        m_workflowList->setCurrentRow(selected);
}

int ComposerSidebar::selectedWorkflow() const
{
    return m_workflowList->currentRow();
}

void ComposerSidebar::setVariables(const QList<Variable>& variables)
{
    clearRows(m_variableRows);

    for (const Variable& variable : variables) {
        auto* row = new QWidget;
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);

        auto* name = new QLabel(u"$"_s + variable.name + u"$"_s);
        name->setObjectName(u"ComposerVarName"_s);
        layout->addWidget(name);

        auto* value = new QLineEdit(variable.value);
        value->setObjectName(u"ComposerVarEdit"_s);
        layout->addWidget(value, 1);

        QPushButton* remove = sidebarButton(icons::minus(), u"Delete this variable"_s);
        remove->setObjectName(u"ComposerRuleArgDelBtn"_s);
        layout->addWidget(remove);

        const QString key = variable.name;

        // editingFinished also fires on focus loss.
        connect(value, &QLineEdit::editingFinished, this,
                [this, key, value]() { emit variableChanged(key, value->text()); });
        connect(remove, &QPushButton::clicked, this,
                [this, key]() { emit variableRemoved(key); });

        m_variableRows->insertWidget(m_variableRows->count() - 1, row);
    }

    // Trailing add row.
    auto* addRow = new QWidget;
    auto* addLayout = new QHBoxLayout(addRow);
    addLayout->setContentsMargins(0, 0, 0, 0);
    addLayout->setSpacing(6);

    auto* addName = new QLineEdit;
    addName->setObjectName(u"ComposerVarEdit"_s);
    addName->setPlaceholderText(u"name"_s);
    addLayout->addWidget(addName);

    auto* addValue = new QLineEdit;
    addValue->setObjectName(u"ComposerVarEdit"_s);
    addValue->setPlaceholderText(u"value"_s);
    addLayout->addWidget(addValue, 1);

    QPushButton* add = sidebarButton(icons::plus(), u"Add a variable"_s);
    addLayout->addWidget(add);

    auto commit = [this, addName, addValue]() {
        const QString name = addName->text().trimmed();
        if (name.isEmpty()) return;
        emit variableAdded(name, addValue->text());
        addName->clear();
        addValue->clear();
    };
    connect(add, &QPushButton::clicked, this, commit);
    connect(addValue, &QLineEdit::returnPressed, this, commit);

    m_variableRows->insertWidget(m_variableRows->count() - 1, addRow);
}

} // namespace tc
