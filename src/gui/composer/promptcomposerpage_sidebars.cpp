// PromptComposerPage — sidebar rebuild logic.
// The composer has three sidebars (rules, vars, workflow); each rebuild
// blows away its container's child widgets and repopulates from the model.

#include <gui/composer/promptcomposerpage.h>
#include <gui/composer/workflowdroplist.h>
#include <core/ruleengine.h>
#include <core/variableindex.h>
#include <core/workflowmanager.h>
#include <utils/appconfig.h>
#include <QCheckBox>
#include <QColor>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>

using namespace core;
using namespace utils;

namespace gui {

void PromptComposerPage::reloadRules()
{
    const QString path = BASE_PATH + "/" + RULES_PATH;
    QStringList errors;
    RuleEngine fresh = RuleEngine::loadFromFile(path, &errors);

    if (!errors.isEmpty()) {
        emit statusMessageRequested(errors.first());
        return;
    }

    m_rules->rules() = fresh.rules();
    emit statusMessageRequested(
        QString("Rules reloaded - %1 rule(s).").arg(m_rules->rules().size()));
    rebuildRulesSidebar();
    QMetaObject::invokeMethod(this, &PromptComposerPage::repush, Qt::QueuedConnection);
}

void PromptComposerPage::reloadVars()
{
    if (!m_varIndex) return;
    const QString path = BASE_PATH + "/" + VARS_PATH;
    VariableIndex fresh = VariableIndex::loadFromFile(path);
    m_varIndex->variables() = fresh.variables();
    emit statusMessageRequested(
        QString("Variables reloaded - %1 variable(s).").arg(m_varIndex->variables().size()));
    rebuildVarsSidebar();
    QMetaObject::invokeMethod(this, &PromptComposerPage::repush, Qt::QueuedConnection);
}

void PromptComposerPage::rebuildRulesSidebar()
{
    while (m_rulesLayout->count() > 0) {
        QLayoutItem* item = m_rulesLayout->takeAt(0);
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }

    QList<Rule>& rules = m_rules->rules();

    if (rules.isEmpty()) {
        auto* hint = new QLabel("No rules defined.\nEdit data/rules.fct to add some.");
        hint->setObjectName("ComposerRulesHint");
        hint->setWordWrap(true);
        hint->setAlignment(Qt::AlignTop);
        m_rulesLayout->addWidget(hint);
    } else {
        for (int i = 0; i < rules.size(); ++i) {
            const bool hasArgEdit = (rules[i].action.type == ActionType::Add
                                  || rules[i].action.type == ActionType::Replace);

            auto* ruleWidget = new QWidget;
            auto* rwl = new QVBoxLayout(ruleWidget);
            rwl->setContentsMargins(0, 0, 0, 2);
            rwl->setSpacing(2);

            auto* cbRow  = new QWidget;
            auto* cbRowL = new QHBoxLayout(cbRow);
            cbRowL->setContentsMargins(0, 0, 0, 0);
            cbRowL->setSpacing(4);

            auto* cb = new QCheckBox(rules[i].name);
            cb->setObjectName("ComposerRuleToggle");
            cb->setChecked(rules[i].enabled);
            connect(cb, &QCheckBox::toggled, this, [this, i](bool on) {
                m_rules->rules()[i].enabled = on;
                if (!m_suppressRuleSave)
                    m_rules->saveToFile(BASE_PATH + "/" + RULES_PATH);
                QMetaObject::invokeMethod(
                    this, &PromptComposerPage::repush, Qt::QueuedConnection);
            });
            cbRowL->addWidget(cb, 1);

            if (rules[i].force) {
                auto* forceBadge = new QLabel("F");
                forceBadge->setObjectName("ComposerRuleForceBadge");
                cbRowL->addWidget(forceBadge);
            }

            rwl->addWidget(cbRow);

            if (hasArgEdit) {
                auto* argEdit = new QLineEdit(rules[i].action.arguments.join(", "));
                argEdit->setObjectName("ComposerRuleArgEdit");
                argEdit->setPlaceholderText("tag to inject…");
                connect(argEdit, &QLineEdit::editingFinished, this, [this, i, argEdit]() {
                    QList<QString> args;
                    for (const QString& a : argEdit->text().split(','))
                        if (const QString t = a.trimmed(); !t.isEmpty())
                            args << t;
                    m_rules->rules()[i].action.arguments = args;
                    m_rules->saveToFile(BASE_PATH + "/" + RULES_PATH);
                    QMetaObject::invokeMethod(
                        this, &PromptComposerPage::repush, Qt::QueuedConnection);
                });

                const bool isReplace = (rules[i].action.type == ActionType::Replace);
                auto* actionBadge = new QLabel(isReplace ? "→" : "+");
                actionBadge->setObjectName(isReplace ? "ComposerRuleReplaceBadge"
                                                     : "ComposerRuleAddBadge");

                auto* argRow = new QWidget;
                auto* arl = new QHBoxLayout(argRow);
                arl->setContentsMargins(18, 0, 0, 0);
                arl->setSpacing(4);
                arl->addWidget(actionBadge);
                arl->addWidget(argEdit, 1);
                rwl->addWidget(argRow);
            }

            m_rulesLayout->addWidget(ruleWidget);
        }
    }

    m_rulesLayout->addStretch();
}

void PromptComposerPage::rebuildWorkflowList()
{
    if (!m_wfList) return;
    m_wfList->clear();

    if (!m_wfManager || m_wfManager->files().isEmpty()) {
        auto* hint = new QListWidgetItem("Drop .json files here");
        hint->setFlags(Qt::NoItemFlags);
        QFont f = hint->font();
        f.setItalic(true);
        hint->setFont(f);
        hint->setForeground(QColor("#2a2a2a"));
        m_wfList->addItem(hint);
        return;
    }

    for (int i = 0; i < m_wfManager->files().size(); ++i) {
        const bool sel = (i == m_wfManager->selectedIndex());
        auto* item = new QListWidgetItem(m_wfManager->files()[i].name);
        item->setData(Qt::UserRole, i);
        item->setToolTip(m_wfManager->files()[i].path);
        if (sel) {
            item->setForeground(QColor("#5a9a5a"));
        }
        m_wfList->addItem(item);
    }
}

void PromptComposerPage::rebuildVarsSidebar()
{
    while (m_varsLayout->count() > 0) {
        QLayoutItem* item = m_varsLayout->takeAt(0);
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }

    if (!m_varIndex || m_varIndex->variables().isEmpty()) {
        auto* hint = new QLabel("No variables defined.\nEdit data/vars.fct to add some.");
        hint->setObjectName("ComposerRulesHint");
        hint->setWordWrap(true);
        m_varsLayout->addWidget(hint);
        return;
    }

    QList<Variable>& vars = m_varIndex->variables();
    for (int i = 0; i < vars.size(); ++i) {
        auto* row = new QWidget;
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(6);

        auto* nameLabel = new QLabel("$" + vars[i].name + "$");
        nameLabel->setObjectName("ComposerVarName");
        nameLabel->setFixedWidth(70);

        auto* edit = new QLineEdit(vars[i].value);
        edit->setObjectName("ComposerVarEdit");
        edit->setPlaceholderText("(empty)");

        connect(edit, &QLineEdit::editingFinished, this, [this, i, edit]() {
            m_varIndex->variables()[i].value = edit->text().trimmed();
            m_varIndex->saveToFile(BASE_PATH + "/" + VARS_PATH);
            QMetaObject::invokeMethod(
                this, &PromptComposerPage::repush, Qt::QueuedConnection);
        });

        rl->addWidget(nameLabel);
        rl->addWidget(edit, 1);
        m_varsLayout->addWidget(row);
    }
}

} // namespace gui
