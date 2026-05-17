// Sidebar rebuilds (rules / vars / workflow). Each rebuild replaces its
// container's children from the current model.

#include <gui/composer/promptcomposerpage.h>
#include <gui/composer/workflowdroplist.h>
#include <core/ruleengine.h>
#include <core/variableindex.h>
#include <core/workflowmanager.h>
#include <utils/appconfig.h>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QCursor>
#include <QEnterEvent>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

using namespace core;
using namespace utils;

namespace gui {

namespace {

// Elides on resize so badges stay visible when a name overflows the column.
class ElidingLabel : public QLabel {
public:
    explicit ElidingLabel(const QString& full, QWidget* parent = nullptr)
        : QLabel(parent), m_full(full)
    {
        setText(full);
        setToolTip(full);
        setMinimumWidth(0);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }

    void setFullText(const QString& full)
    {
        m_full = full;
        setToolTip(full);
        QFontMetrics fm(font());
        setText(fm.elidedText(full, Qt::ElideRight, width()));
    }

protected:
    void resizeEvent(QResizeEvent* e) override
    {
        QLabel::resizeEvent(e);
        QFontMetrics fm(font());
        setText(fm.elidedText(m_full, Qt::ElideRight, e->size().width()));
    }

private:
    QString m_full;
};

// Click-to-edit rule name. Both children share one QHBoxLayout slot; only
// one is visible at a time. Qt's layout skips hidden children for sizing,
// so the row stays QLabel-tall in display mode (QStackedWidget would have
// inflated it to the QLineEdit's height via QStackedLayout::minimumSize).
// onCommit returns true to accept (widget shows the new text) or false to
// revert (duplicate/empty validation).
class EditableRuleName : public QWidget {
public:
    explicit EditableRuleName(const QString& full, QWidget* parent = nullptr)
        : QWidget(parent),
          m_label(new ElidingLabel(full, this)),
          m_edit(new QLineEdit(this)),
          m_text(full)
    {
        m_label->setObjectName("ComposerRuleName");
        m_edit->setObjectName("ComposerRuleNameEdit");
        auto* l = new QHBoxLayout(this);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(0);
        l->addWidget(m_label);
        l->addWidget(m_edit);
        m_edit->hide();
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_label->installEventFilter(this);
        m_edit->installEventFilter(this);
    }

    std::function<bool(const QString&)> onCommit;

    void beginEdit()
    {
        if (m_edit->isVisible()) return;
        m_edit->setText(m_text);
        m_edit->selectAll();
        m_label->hide();
        m_edit->show();
        m_edit->setFocus(Qt::OtherFocusReason);
    }

protected:
    bool eventFilter(QObject* o, QEvent* e) override
    {
        if (o == m_label && e->type() == QEvent::MouseButtonDblClick) {
            beginEdit();
            return true;
        }
        if (o == m_edit) {
            if (e->type() == QEvent::FocusOut) {
                commitEdit();
            }
            else if (e->type() == QEvent::KeyPress) {
                auto* ke = static_cast<QKeyEvent*>(e);
                if (ke->key() == Qt::Key_Escape) {
                    cancelEdit();
                    return true;
                }
                if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
                    commitEdit();
                    return true;
                }
            }
        }
        return false;
    }

private:
    void swapToLabel()
    {
        if (m_label->isVisible()) return;
        m_edit->hide();
        m_label->show();
    }

    void commitEdit()
    {
        if (m_committing || !m_edit->isVisible()) return;
        m_committing = true;
        const QString next = m_edit->text().trimmed();
        if (!next.isEmpty() && next != m_text && onCommit && onCommit(next)) {
            m_text = next;
            m_label->setFullText(next);
        }
        swapToLabel();
        m_committing = false;
    }

    void cancelEdit()
    {
        swapToLabel();
    }

    ElidingLabel* m_label;
    QLineEdit* m_edit;
    QString m_text;
    bool m_committing = false;
};

// Rule row; hides args area unless hovered or focused. Hide is debounced
// and checks live QCursor::pos() (underMouse() lags during collapse).
class RuleRow : public QWidget {
public:
    explicit RuleRow(QWidget* parent = nullptr) : QWidget(parent)
    {
        m_hideTimer = new QTimer(this);
        m_hideTimer->setSingleShot(true);
        m_hideTimer->setInterval(150);
        connect(m_hideTimer, &QTimer::timeout, this, [this]() { tryHide(); });
    }

    void setArgsArea(QWidget* a)
    {
        m_args = a;
        if (!m_args) return;
        m_args->setVisible(false);
        // Watch every child so the row stays open while typing.
        for (QWidget* w : m_args->findChildren<QWidget*>())
            w->installEventFilter(this);
    }

protected:
    void enterEvent(QEnterEvent*) override
    {
        m_hideTimer->stop();
        if (m_args) m_args->setVisible(true);
    }
    void leaveEvent(QEvent*) override
    {
        m_hideTimer->start();
    }

    bool eventFilter(QObject*, QEvent* e) override
    {
        if (e->type() == QEvent::FocusIn) {
            m_hideTimer->stop();
            if (m_args) m_args->setVisible(true);
        }
        else if (e->type() == QEvent::FocusOut) {
            m_hideTimer->start();
        }
        return false;
    }

private:
    void tryHide()
    {
        if (!m_args || !m_args->isVisible()) return;
        const QPoint g = QCursor::pos();
        const QRect rg = QRect(mapToGlobal(QPoint(0, 0)), size());
        if (rg.contains(g)) return;
        // Keep open only when focus is inside args (a line edit being typed in).
        // Checking the whole row would keep open after toggling the checkbox.
        if (QWidget* fw = QApplication::focusWidget())
            if (m_args->isAncestorOf(fw)) return;
        m_args->setVisible(false);
    }

    QWidget* m_args = nullptr;
    QTimer* m_hideTimer = nullptr;
};

// Glyph-only badge for rule rows; QSS objectName drives the color.
QLabel* makeBadge(const QString& glyph, const QString& objectName)
{
    auto* lbl = new QLabel;
    lbl->setObjectName(objectName);
    lbl->setText(glyph);
    return lbl;
}

} // namespace

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
    captureUndoSnapshot();
    rebuildRulesSidebar();
    queueRepush();
}

void PromptComposerPage::reloadVars()
{
    if (!m_varIndex) return;
    const QString path = BASE_PATH + "/" + VARS_PATH;
    VariableIndex fresh = VariableIndex::loadFromFile(path);
    m_varIndex->variables() = fresh.variables();
    emit statusMessageRequested(
        QString("Variables reloaded - %1 variable(s).").arg(m_varIndex->variables().size()));
    captureUndoSnapshot();
    rebuildVarsSidebar();
    queueRepush();
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
    }
    else {
        for (int i = 0; i < rules.size(); ++i) {
            const bool hasArgEdit = (rules[i].action.type == ActionType::Add ||
                                     rules[i].action.type == ActionType::Replace);
            const bool isReplace = (rules[i].action.type == ActionType::Replace);

            auto* ruleWidget = new RuleRow;
            auto* rwl = new QVBoxLayout(ruleWidget);
            rwl->setContentsMargins(0, 0, 0, 2);
            rwl->setSpacing(2);

            ruleWidget->setContextMenuPolicy(Qt::CustomContextMenu);

            // ---- Header row: indicator-only checkbox + elided name + badges
            auto* cbRow = new QWidget;
            auto* cbRowL = new QHBoxLayout(cbRow);
            cbRowL->setContentsMargins(0, 0, 0, 0);
            cbRowL->setSpacing(4);

            auto* cb = new QCheckBox;
            cb->setObjectName("ComposerRuleToggle");
            cb->setChecked(rules[i].enabled);
            connect(cb, &QCheckBox::toggled, this, [this, i](bool on) {
                m_rules->rules()[i].enabled = on;
                if (!m_suppressRuleSave) m_rules->saveToFile(BASE_PATH + "/" + RULES_PATH);
                captureUndoSnapshot();
                queueRepush();
            });
            cbRowL->addWidget(cb);

            auto* nameWidget = new EditableRuleName(rules[i].name);
            nameWidget->onCommit = [this, i](const QString& next) -> bool {
                if (i >= m_rules->rules().size()) return false;
                for (int j = 0; j < m_rules->rules().size(); ++j)
                    if (j != i && m_rules->rules()[j].name == next) return false;
                m_rules->rules()[i].name = next;
                m_rules->saveToFile(BASE_PATH + "/" + RULES_PATH);
                captureUndoSnapshot();
                // Refresh "^ <ruleSource>" badges on already-matched tags.
                queueRepush();
                return true;
            };
            cbRowL->addWidget(nameWidget, 1);

            connect(ruleWidget, &QWidget::customContextMenuRequested, this,
                    [this, ruleWidget, nameWidget, i](const QPoint& pos) {
                        QMenu menu;
                        // Deferred so the menu's focus restoration doesn't steal
                        // focus back from the rename QLineEdit.
                        menu.addAction("Rename...", this, [nameWidget]() {
                            QTimer::singleShot(0, nameWidget,
                                               [nameWidget]() { nameWidget->beginEdit(); });
                        });
                        menu.addAction("Delete", this, [this, i]() {
                            if (i >= m_rules->rules().size()) return;
                            m_rules->rules().removeAt(i);
                            m_rules->saveToFile(BASE_PATH + "/" + RULES_PATH);
                            captureUndoSnapshot();
                            rebuildRulesSidebar();
                            queueRepush();
                        });
                        menu.addSeparator();
                        menu.addAction("Add rule...", this, [this]() { promptAddRule(); });
                        menu.exec(ruleWidget->mapToGlobal(pos));
                    });

            if (rules[i].force) {
                cbRowL->addWidget(makeBadge("⚑", "ComposerRuleForceBadge"));
            }

            if (hasArgEdit) {
                // Heavy Greek Cross sits at cap-height; ASCII '+' renders
                // lower (math baseline) and breaks alignment with neighbors.
                cbRowL->addWidget(
                    makeBadge(isReplace ? "⇄" : "✚",
                              isReplace ? "ComposerRuleReplaceBadge" : "ComposerRuleAddBadge"));
            }

            rwl->addWidget(cbRow);

            // ---- Per-tag rows: one QLineEdit per argument + trailing add row
            if (hasArgEdit) {
                auto* argsCol = new QWidget;
                auto* acl = new QVBoxLayout(argsCol);
                acl->setContentsMargins(18, 0, 0, 0);
                acl->setSpacing(2);

                const QList<QString>& args = rules[i].action.arguments;
                for (int k = 0; k < args.size(); ++k) {
                    auto* row = new QWidget;
                    auto* rl = new QHBoxLayout(row);
                    rl->setContentsMargins(0, 0, 0, 0);
                    rl->setSpacing(4);

                    auto* edit = new QLineEdit(args[k]);
                    edit->setObjectName("ComposerRuleArgEdit");
                    edit->setPlaceholderText("tag…");
                    if (m_danbooruIndex) new TagLineAutocomplete(edit, m_danbooruIndex, edit);
                    connect(edit, &QLineEdit::editingFinished, this, [this, i, k, edit]() {
                        QList<QString>& a = m_rules->rules()[i].action.arguments;
                        if (k >= a.size()) return;
                        const QString t = edit->text().trimmed();
                        if (t == a[k]) return;
                        if (t.isEmpty())
                            a.removeAt(k);
                        else
                            a[k] = t;
                        m_rules->saveToFile(BASE_PATH + "/" + RULES_PATH);
                        captureUndoSnapshot();
                        rebuildRulesSidebar();
                        queueRepush();
                    });

                    auto* delBtn = new QPushButton("✕");
                    delBtn->setObjectName("ComposerRuleArgDelBtn");
                    delBtn->setCursor(Qt::PointingHandCursor);
                    delBtn->setFocusPolicy(Qt::NoFocus);
                    delBtn->setFixedSize(20, 20);
                    connect(delBtn, &QPushButton::clicked, this, [this, i, k]() {
                        QList<QString>& a = m_rules->rules()[i].action.arguments;
                        if (k >= a.size()) return;
                        a.removeAt(k);
                        m_rules->saveToFile(BASE_PATH + "/" + RULES_PATH);
                        captureUndoSnapshot();
                        rebuildRulesSidebar();
                        queueRepush();
                    });

                    rl->addWidget(edit, 1);
                    rl->addWidget(delBtn);
                    acl->addWidget(row);
                }

                // Trailing add row: commit pushes a new arg and rebuilds.
                auto* addEdit = new QLineEdit;
                addEdit->setObjectName("ComposerRuleArgEdit");
                addEdit->setPlaceholderText("add tag…");
                if (m_danbooruIndex) new TagLineAutocomplete(addEdit, m_danbooruIndex, addEdit);
                connect(addEdit, &QLineEdit::editingFinished, this, [this, i, addEdit]() {
                    const QString t = addEdit->text().trimmed();
                    if (t.isEmpty()) return;
                    m_rules->rules()[i].action.arguments << t;
                    m_rules->saveToFile(BASE_PATH + "/" + RULES_PATH);
                    captureUndoSnapshot();
                    rebuildRulesSidebar();
                    queueRepush();
                });
                acl->addWidget(addEdit);

                rwl->addWidget(argsCol);
                ruleWidget->setArgsArea(argsCol);
            }

            m_rulesLayout->addWidget(ruleWidget);
        }
    }

    m_rulesLayout->addStretch();
}

void PromptComposerPage::promptAddRule()
{
    bool ok = false;
    const QString raw = QInputDialog::getText(this, "Add Rule", "Name:", QLineEdit::Normal,
                                              QString(), &ok);
    if (!ok) return;
    const QString name = raw.trimmed();
    if (name.isEmpty()) return;
    for (const auto& r : m_rules->rules())
        if (r.name == name) {
            emit statusMessageRequested(QString("Rule \"%1\" already exists.").arg(name));
            return;
        }

    core::Rule r;
    r.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    r.name = name;
    r.enabled = true;
    r.action.type = core::ActionType::Skip;
    m_rules->rules().append(r);
    m_rules->saveToFile(BASE_PATH + "/" + RULES_PATH);
    captureUndoSnapshot();
    rebuildRulesSidebar();
    queueRepush();
    emit statusMessageRequested(
        QString("Added rule \"%1\" - edit rules.fct to define its match.").arg(name));
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

    const QString filter = m_wfFilter ? m_wfFilter->text().trimmed() : QString();
    for (int i = 0; i < m_wfManager->files().size(); ++i) {
        const auto& wf = m_wfManager->files()[i];
        if (!filter.isEmpty() && !wf.name.contains(filter, Qt::CaseInsensitive)) continue;
        const bool sel = (i == m_wfManager->selectedIndex());
        auto* item = new QListWidgetItem(wf.name);
        item->setData(Qt::UserRole, i);
        item->setToolTip(wf.path);
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

    if (!m_varIndex) {
        auto* hint = new QLabel("Variable index unavailable.");
        hint->setObjectName("ComposerRulesHint");
        hint->setWordWrap(true);
        m_varsLayout->addWidget(hint);
        return;
    }

    // Rebuild keeps focus on the "add name" field for successive adds.
    auto persistAndRepush = [this]() {
        m_varIndex->saveToFile(BASE_PATH + "/" + VARS_PATH);
        captureUndoSnapshot();
        rebuildVarsSidebar();
        queueRepush();
    };

    QList<Variable>& vars = m_varIndex->variables();
    for (int i = 0; i < vars.size(); ++i) {
        auto* row = new QWidget;
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(6);

        auto* nameLabel = new QLabel("$" + vars[i].name + "$");
        nameLabel->setObjectName("ComposerVarName");
        nameLabel->setFixedWidth(70);

        auto* edit = new QLineEdit(vars[i].value);
        edit->setObjectName("ComposerVarEdit");
        edit->setPlaceholderText("(empty)");

        connect(edit, &QLineEdit::editingFinished, this, [this, i, edit]() {
            if (i >= m_varIndex->variables().size()) return;
            const QString next = edit->text().trimmed();
            if (m_varIndex->variables()[i].value == next) return;
            m_varIndex->variables()[i].value = next;
            m_varIndex->saveToFile(BASE_PATH + "/" + VARS_PATH);
            captureUndoSnapshot();
            queueRepush();
        });

        // Reuses the rules-arg delete button styling. NoFocus so Tab
        // stays on the value edits.
        auto* delBtn = new QPushButton("✕");
        delBtn->setObjectName("ComposerRuleArgDelBtn");
        delBtn->setCursor(Qt::PointingHandCursor);
        delBtn->setFocusPolicy(Qt::NoFocus);
        delBtn->setFixedSize(20, 20);
        delBtn->setToolTip("Remove variable");
        connect(delBtn, &QPushButton::clicked, this, [this, i, persistAndRepush]() {
            if (i >= m_varIndex->variables().size()) return;
            const QString token = "$" + m_varIndex->variables()[i].name + "$";
            int usage = 0;
            for (const QString& t : m_activeTags)
                if (t.contains(token)) ++usage;
            for (auto it = m_activePushes.cbegin(); it != m_activePushes.cend(); ++it)
                for (const QString& t : it.value())
                    if (t.contains(token)) ++usage;
            m_varIndex->variables().removeAt(i);
            persistAndRepush();
            if (usage > 0)
                emit statusMessageRequested(
                    QString("Removed %1 - still referenced by %2 tag%3.")
                        .arg(token)
                        .arg(usage)
                        .arg(usage == 1 ? "" : "s"));
        });

        rl->addWidget(nameLabel);
        rl->addWidget(edit, 1);
        rl->addWidget(delBtn);
        m_varsLayout->addWidget(row);
    }

    // Trailing add row: Enter commits. editingFinished would fire when
    // tabbing between fields with a half-typed value.
    auto* addRow = new QWidget;
    auto* arl = new QHBoxLayout(addRow);
    arl->setContentsMargins(0, 0, 0, 0);
    arl->setSpacing(6);

    auto* addName = new QLineEdit;
    addName->setObjectName("ComposerVarEdit");
    addName->setPlaceholderText("name…");
    addName->setFixedWidth(70);

    auto* addValue = new QLineEdit;
    addValue->setObjectName("ComposerVarEdit");
    addValue->setPlaceholderText("value…");

    auto commit = [this, addName, addValue, persistAndRepush]() {
        const QString name = addName->text().trimmed();
        if (name.isEmpty()) return;
        // Duplicate name: clear+refocus signals it visually.
        for (const Variable& v : m_varIndex->variables())
            if (v.name == name) {
                addName->clear();
                addValue->clear();
                addName->setFocus();
                return;
            }
        Variable nv;
        nv.name = name;
        nv.value = addValue->text().trimmed();
        m_varIndex->variables() << nv;
        persistAndRepush();
    };
    connect(addName, &QLineEdit::returnPressed, this, commit);
    connect(addValue, &QLineEdit::returnPressed, this, commit);

    arl->addWidget(addName);
    arl->addWidget(addValue, 1);
    m_varsLayout->addWidget(addRow);
    m_varsLayout->addStretch();
}

} // namespace gui
