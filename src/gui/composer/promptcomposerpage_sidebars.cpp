// Sidebar rebuild logic for PromptComposerPage. Each rebuild
// (rules / vars / workflow) replaces its container's children
// from the current model.

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
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>

using namespace core;
using namespace utils;

namespace gui {

namespace {

// QLabel that elides its text on resize so adjacent badges stay visible
// when a rule's name is longer than the column.
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

// Rule row that hides its args area unless hovered or focused, to keep
// the sidebar compact. Hide is debounced and checked against the live
// QCursor::pos() (underMouse() lags geometry changes during collapse).
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
        // Watch focus on every child so we can keep open while typing.
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
        // Use the live cursor position; underMouse() lags geometry changes.
        const QPoint g = QCursor::pos();
        const QRect rg = QRect(mapToGlobal(QPoint(0, 0)), size());
        if (rg.contains(g)) return;
        // Stay open only when focus is inside the args area (a line edit
        // the user is typing into). Checking the whole row would falsely
        // trigger keep-open after toggling the rule's checkbox.
        if (QWidget* fw = QApplication::focusWidget())
            if (m_args->isAncestorOf(fw)) return;
        m_args->setVisible(false);
    }

    QWidget* m_args = nullptr;
    QTimer* m_hideTimer = nullptr;
};

// Glyph-only status badge for rule rows; QSS objectName drives the color.
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

            // ── Header row: indicator-only checkbox + elided name + badges ──
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
                QMetaObject::invokeMethod(this, &PromptComposerPage::repush, Qt::QueuedConnection);
            });
            cbRowL->addWidget(cb);

            auto* nameLabel = new ElidingLabel(rules[i].name);
            nameLabel->setObjectName("ComposerRuleName");
            cbRowL->addWidget(nameLabel, 1);

            if (rules[i].force) {
                cbRowL->addWidget(makeBadge("⚑", "ComposerRuleForceBadge"));
            }

            if (hasArgEdit) {
                // Heavy Greek Cross (U+271A) instead of ASCII '+'; the
                // ASCII plus sits on the math baseline so it renders lower
                // than the flag/arrow glyphs (cap-height metrics).
                cbRowL->addWidget(
                    makeBadge(isReplace ? "⇄" : "✚",
                              isReplace ? "ComposerRuleReplaceBadge" : "ComposerRuleAddBadge"));
            }

            rwl->addWidget(cbRow);

            // ── Per-tag rows: one QLineEdit per argument + trailing add row ──
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
                        rebuildRulesSidebar();
                        QMetaObject::invokeMethod(this, &PromptComposerPage::repush,
                                                  Qt::QueuedConnection);
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
                        rebuildRulesSidebar();
                        QMetaObject::invokeMethod(this, &PromptComposerPage::repush,
                                                  Qt::QueuedConnection);
                    });

                    rl->addWidget(edit, 1);
                    rl->addWidget(delBtn);
                    acl->addWidget(row);
                }

                // Trailing add row - committing pushes a new arg and rebuilds.
                auto* addEdit = new QLineEdit;
                addEdit->setObjectName("ComposerRuleArgEdit");
                addEdit->setPlaceholderText("add tag…");
                connect(addEdit, &QLineEdit::editingFinished, this, [this, i, addEdit]() {
                    const QString t = addEdit->text().trimmed();
                    if (t.isEmpty()) return;
                    m_rules->rules()[i].action.arguments << t;
                    m_rules->saveToFile(BASE_PATH + "/" + RULES_PATH);
                    rebuildRulesSidebar();
                    QMetaObject::invokeMethod(this, &PromptComposerPage::repush,
                                              Qt::QueuedConnection);
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

    if (!m_varIndex) {
        auto* hint = new QLabel("Variable index unavailable.");
        hint->setObjectName("ComposerRulesHint");
        hint->setWordWrap(true);
        m_varsLayout->addWidget(hint);
        return;
    }

    // Save vars.fct and rebuild; rebuild keeps focus anchored to the
    // "add name" field for easy successive adds.
    auto persistAndRepush = [this]() {
        m_varIndex->saveToFile(BASE_PATH + "/" + VARS_PATH);
        rebuildVarsSidebar();
        QMetaObject::invokeMethod(this, &PromptComposerPage::repush, Qt::QueuedConnection);
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
            m_varIndex->variables()[i].value = edit->text().trimmed();
            m_varIndex->saveToFile(BASE_PATH + "/" + VARS_PATH);
            QMetaObject::invokeMethod(this, &PromptComposerPage::repush, Qt::QueuedConnection);
        });

        // Reuse the rules-arg delete button styling: dim glyph that
        // brightens on hover. NoFocus so Tab stays on the value edits.
        auto* delBtn = new QPushButton("✕");
        delBtn->setObjectName("ComposerRuleArgDelBtn");
        delBtn->setCursor(Qt::PointingHandCursor);
        delBtn->setFocusPolicy(Qt::NoFocus);
        delBtn->setFixedSize(20, 20);
        delBtn->setToolTip("Remove variable");
        connect(delBtn, &QPushButton::clicked, this, [this, i, persistAndRepush]() {
            if (i >= m_varIndex->variables().size()) return;
            m_varIndex->variables().removeAt(i);
            persistAndRepush();
        });

        rl->addWidget(nameLabel);
        rl->addWidget(edit, 1);
        rl->addWidget(delBtn);
        m_varsLayout->addWidget(row);
    }

    // Trailing add row: Enter on either field commits. We avoid
    // editingFinished here since tabbing between the two fields would
    // fire it with a half-typed value.
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
        // Quietly ignore a duplicate name - the placeholder + reset below
        // makes it obvious nothing was added.
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
}

} // namespace gui
