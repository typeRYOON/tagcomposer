// Sidebar rebuilds: rules, variables, workflows and profiles.

#include <app/composer_page.h>
#include <app/app_data.h>
#include <app/composer_widgets.h>
#include <app/icons.h>
#include <app/paths.h>
#include <app/tag_search_bar.h>
#include <app/widget_utils.h>
#include <core/rule_io.h>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDir>
#include <QEnterEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QResizeEvent>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <functional>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

// Elides on resize so the badges stay visible when a name overflows.
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
        setText(QFontMetrics(font()).elidedText(full, Qt::ElideRight, width()));
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        setText(QFontMetrics(font()).elidedText(m_full, Qt::ElideRight, event->size().width()));
    }

private:
    QString m_full;
};

// Double-click-to-edit rule name. Label and edit swap visibility; a stacked
// widget would size the row to the edit. onCommit returns false to revert.
class EditableRuleName : public QWidget {
public:
    explicit EditableRuleName(const QString& full, QWidget* parent = nullptr)
        : QWidget(parent), m_label(new ElidingLabel(full, this)), m_edit(new QLineEdit(this)),
          m_text(full)
    {
        m_label->setObjectName(u"ComposerRuleName"_s);
        m_edit->setObjectName(u"ComposerRuleNameEdit"_s);

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(m_label);
        layout->addWidget(m_edit);

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
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_label && event->type() == QEvent::MouseButtonDblClick) {
            beginEdit();
            return true;
        }
        if (watched != m_edit) return false;

        if (event->type() == QEvent::FocusOut) {
            commitEdit();
            return false;
        }
        if (event->type() != QEvent::KeyPress) return false;

        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            swapToLabel();
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            commitEdit();
            return true;
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

    ElidingLabel* m_label;
    QLineEdit* m_edit;
    QString m_text;
    bool m_committing = false;
};

// Shows its arguments while hovered or focused. The hide is debounced and
// checks the cursor, since underMouse() lags while collapsing.
class RuleRow : public QWidget {
public:
    explicit RuleRow(QWidget* parent = nullptr) : QWidget(parent)
    {
        m_hideTimer = new QTimer(this);
        m_hideTimer->setSingleShot(true);
        m_hideTimer->setInterval(150);
        connect(m_hideTimer, &QTimer::timeout, this, [this]() { tryHide(); });
    }

    void setArgsArea(QWidget* area)
    {
        m_args = area;
        if (!m_args) return;

        m_args->setVisible(false);
        // Stay open while a child has focus.
        for (QWidget* child : m_args->findChildren<QWidget*>())
            child->installEventFilter(this);
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

    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::FocusIn) {
            m_hideTimer->stop();
            if (m_args) m_args->setVisible(true);
        } else if (event->type() == QEvent::FocusOut) {
            m_hideTimer->start();
        }
        return false;
    }

private:
    void tryHide()
    {
        if (!m_args || !m_args->isVisible()) return;
        if (QRect(mapToGlobal(QPoint(0, 0)), size()).contains(QCursor::pos())) return;

        // Stay open only for focus inside the arguments, not the checkbox.
        if (QWidget* focused = QApplication::focusWidget())
            if (m_args->isAncestorOf(focused)) return;

        m_args->setVisible(false);
    }

    QWidget* m_args = nullptr;
    QTimer* m_hideTimer = nullptr;
};

// Colored via the icon; QSS can't recolor a pixmap.
QLabel* makeBadge(const QIcon& icon, int px, const QString& objectName)
{
    auto* label = new QLabel;
    label->setObjectName(objectName);
    label->setPixmap(icon.pixmap(px, px));
    return label;
}

QPushButton* argDeleteButton(const QString& tooltip = {})
{
    auto* button = new QPushButton;
    button->setObjectName(u"ComposerRuleArgDelBtn"_s);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedSize(20, 20);
    if (!tooltip.isEmpty()) button->setToolTip(tooltip);
    icons::applyStates(button, icons::close, 10, QColor(0x44, 0x44, 0x44),
                       QColor(0xaa, 0x66, 0x66));
    return button;
}

void clearLayout(QVBoxLayout* layout)
{
    while (layout->count() > 0) {
        QLayoutItem* item = layout->takeAt(0);
        if (QWidget* widget = item->widget()) widget->deleteLater();
        delete item;
    }
}

} // namespace

// ---- Reloads

void ComposerPage::reloadRules()
{
    const QString error = m_data->reloadRules();
    if (!error.isEmpty()) {
        emit statusMessage(error);
        return;
    }

    emit statusMessage(
        u"Rules reloaded - %1 rule(s)."_s.arg(m_data->ruleFile.rules.size()));
    rebuildRulesSidebar();
    refresh();
}

void ComposerPage::reloadVars()
{
    const QString error = m_data->reloadVariables();
    if (!error.isEmpty()) {
        emit statusMessage(error);
        return;
    }

    emit statusMessage(
        u"Variables reloaded - %1 variable(s)."_s.arg(m_data->varsFile.vars.all().size()));
    rebuildVarsSidebar();
    refresh();
}

// ---- Profiles

void ComposerPage::rebuildProfilesSidebar()
{
    if (!m_groupProfileBox || !m_formatProfileBox) return;

    const QSignalBlocker blockGroup(m_groupProfileBox);
    const QSignalBlocker blockFormat(m_formatProfileBox);
    m_groupProfileBox->clear();
    m_formatProfileBox->clear();

    for (const GroupProfile& profile : m_profiles.groupProfiles())
        m_groupProfileBox->addItem(profile.name, profile.name);
    for (const FormatProfile& profile : m_profiles.formatProfiles())
        m_formatProfileBox->addItem(profile.name, profile.name);

    // Items carry their profile name; the "(from state)" and fallback rows don't.
    auto select = [](QComboBox* box, const QString& active, const QString& oneOff,
                     const QString& fallback) {
        if (oneOff.isEmpty()) {
            const int index = box->findData(active);
            if (index >= 0) {
                box->setCurrentIndex(index);
                return;
            }
        }
        box->addItem(oneOff.isEmpty() ? fallback : oneOff, QString());
        box->setCurrentIndex(box->count() - 1);
    };

    select(m_groupProfileBox, m_profiles.activeGroup(), m_oneOffGroupLabel,
           u"(groups.fct order)"_s);
    select(m_formatProfileBox, m_profiles.activeFormat(), m_oneOffFormatLabel,
           u"(settings.json)"_s);
}

void ComposerPage::applyProfileStamp(const SavedState& state, bool refreshNow)
{
    if (!state.profilesStamped) return;

    // Match on content so a renamed profile still selects; ties prefer the name.
    QString groupSelection;
    for (const GroupProfile& profile : m_profiles.groupProfiles()) {
        if (applyGroupOrder(m_data->groups, profile.order).names() != state.groupOrder) continue;
        if (groupSelection.isEmpty() || profile.name == state.groupProfileName)
            groupSelection = profile.name;
    }

    QString formatSelection;
    for (const FormatProfile& profile : m_profiles.formatProfiles()) {
        if (profile.formats != state.facetFormats) continue;
        if (formatSelection.isEmpty() || profile.name == state.formatProfileName)
            formatSelection = profile.name;
    }

    if (!groupSelection.isEmpty()) m_profiles.setActiveGroup(groupSelection);
    if (!formatSelection.isEmpty()) m_profiles.setActiveFormat(formatSelection);
    if (!groupSelection.isEmpty() || !formatSelection.isEmpty())
        m_profiles.saveActiveToFile(m_data->dataPath(paths::kProfiles));

    auto oneOff = [](const QString& name) {
        return name.isEmpty() ? u"(from state)"_s : name + u" (from state)"_s;
    };
    m_oneOffGroupLabel = groupSelection.isEmpty() ? oneOff(state.groupProfileName) : QString();
    m_oneOffFormatLabel = formatSelection.isEmpty() ? oneOff(state.formatProfileName) : QString();

    // The snapshot wins, so a state replays as saved even if the profile changed.
    m_facetFormats = state.facetFormats;
    m_groups = applyGroupOrder(m_data->groups, state.groupOrder);

    rebuildProfilesSidebar();
    if (!refreshNow) return;

    m_freezeNextRebuild = true;
    applyTagFilter();
}

// ---- Rules

void ComposerPage::rebuildRulesSidebar()
{
    clearLayout(m_rulesLayout);

    QList<Rule>& rules = m_data->ruleFile.rules;
    if (rules.isEmpty()) {
        auto* hint = new QLabel(u"No rules defined.\nEdit data/system/rules.fct to add some."_s);
        hint->setObjectName(u"ComposerRulesHint"_s);
        hint->setWordWrap(true);
        hint->setAlignment(Qt::AlignTop);
        m_rulesLayout->addWidget(hint);
        m_rulesLayout->addStretch();
        return;
    }

    for (int i = 0; i < int(rules.size()); ++i) {
        const bool injects = rules[i].action.type == ActionType::Add
            || rules[i].action.type == ActionType::Replace;
        const bool isReplace = rules[i].action.type == ActionType::Replace;

        auto* ruleWidget = new RuleRow;
        ruleWidget->setContextMenuPolicy(Qt::CustomContextMenu);

        auto* ruleLayout = new QVBoxLayout(ruleWidget);
        ruleLayout->setContentsMargins(0, 0, 0, 2);
        ruleLayout->setSpacing(2);

        // ---- Header: the toggle, the name, and the badges
        auto* headerRow = new QWidget;
        auto* headerLayout = new QHBoxLayout(headerRow);
        headerLayout->setContentsMargins(0, 0, 0, 0);
        headerLayout->setSpacing(4);

        auto* toggle = new QCheckBox;
        toggle->setObjectName(u"ComposerRuleToggle"_s);
        toggle->setChecked(rules[i].enabled);
        connect(toggle, &QCheckBox::toggled, this, [this, i](bool on) {
            if (i >= m_data->ruleFile.rules.size()) return;
            m_data->ruleFile.rules[i].enabled = on;
            report(m_data->saveRules());
            refresh();
        });
        headerLayout->addWidget(toggle);

        auto* nameWidget = new EditableRuleName(rules[i].name);
        nameWidget->onCommit = [this, i](const QString& next) {
            if (i >= m_data->ruleFile.rules.size()) return false;
            m_data->ruleFile.rules[i].name = next;
            report(m_data->saveRules());
            refresh(); // row badges show rule names
            return true;
        };
        headerLayout->addWidget(nameWidget, 1);

        connect(ruleWidget, &QWidget::customContextMenuRequested, this,
                [this, ruleWidget, nameWidget, i](const QPoint& pos) {
                    QMenu menu;
                    // Deferred so the closing menu doesn't steal focus back.
                    menu.addAction(u"Rename..."_s, this, [nameWidget]() {
                        QTimer::singleShot(0, nameWidget,
                                           [nameWidget]() { nameWidget->beginEdit(); });
                    });
                    menu.addAction(u"Delete"_s, this, [this, i]() {
                        if (i >= m_data->ruleFile.rules.size()) return;
                        m_data->ruleFile.rules.removeAt(i);
                        report(m_data->saveRules());
                        rebuildRulesSidebar();
                        refresh();
                    });
                    menu.addSeparator();
                    menu.addAction(u"Add rule..."_s, this, [this]() { promptAddRule(); });
                    menu.exec(ruleWidget->mapToGlobal(pos));
                });

        if (rules[i].force)
            headerLayout->addWidget(makeBadge(icons::flag(12, QColor(0x66, 0x44, 0x22)), 12,
                                              u"ComposerRuleForceBadge"_s));

        if (injects)
            headerLayout->addWidget(
                makeBadge(isReplace ? icons::swap(13, QColor(0x66, 0x44, 0x22))
                                    : icons::plus(13, QColor(0x33, 0x66, 0x44)),
                          13,
                          isReplace ? u"ComposerRuleReplaceBadge"_s
                                    : u"ComposerRuleAddBadge"_s));

        ruleLayout->addWidget(headerRow);

        // ---- One edit per injected tag, plus a trailing add row
        if (!injects) {
            m_rulesLayout->addWidget(ruleWidget);
            continue;
        }

        auto* argsColumn = new QWidget;
        auto* argsLayout = new QVBoxLayout(argsColumn);
        argsLayout->setContentsMargins(18, 0, 0, 0);
        argsLayout->setSpacing(2);

        const QStringList& arguments = rules[i].action.arguments;
        for (int k = 0; k < int(arguments.size()); ++k) {
            auto* row = new QWidget;
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(4);

            auto* edit = new QLineEdit(arguments[k]);
            edit->setObjectName(u"ComposerRuleArgEdit"_s);
            edit->setPlaceholderText(u"tag"_s);
            new TagLineAutocomplete(edit, &m_data->danbooru, edit);

            connect(edit, &QLineEdit::editingFinished, this, [this, i, k, edit]() {
                if (i >= m_data->ruleFile.rules.size()) return;
                QStringList& args = m_data->ruleFile.rules[i].action.arguments;
                if (k >= args.size()) return;

                const QString text = edit->text().trimmed();
                if (text == args[k]) return;

                if (text.isEmpty())
                    args.removeAt(k);
                else
                    args[k] = text;

                report(m_data->saveRules());
                rebuildRulesSidebar();
                refresh();
            });

            QPushButton* deleteBtn = argDeleteButton();
            connect(deleteBtn, &QPushButton::clicked, this, [this, i, k]() {
                if (i >= m_data->ruleFile.rules.size()) return;
                QStringList& args = m_data->ruleFile.rules[i].action.arguments;
                if (k >= args.size()) return;

                args.removeAt(k);
                report(m_data->saveRules());
                rebuildRulesSidebar();
                refresh();
            });

            rowLayout->addWidget(edit, 1);
            rowLayout->addWidget(deleteBtn);
            argsLayout->addWidget(row);
        }

        auto* addEdit = new QLineEdit;
        addEdit->setObjectName(u"ComposerRuleArgEdit"_s);
        addEdit->setPlaceholderText(u"add tag"_s);
        new TagLineAutocomplete(addEdit, &m_data->danbooru, addEdit);

        connect(addEdit, &QLineEdit::editingFinished, this, [this, i, addEdit]() {
            const QString text = addEdit->text().trimmed();
            if (text.isEmpty() || i >= m_data->ruleFile.rules.size()) return;

            m_data->ruleFile.rules[i].action.arguments << text;
            report(m_data->saveRules());
            rebuildRulesSidebar();
            refresh();
        });
        argsLayout->addWidget(addEdit);

        ruleLayout->addWidget(argsColumn);
        ruleWidget->setArgsArea(argsColumn);
        m_rulesLayout->addWidget(ruleWidget);
    }

    m_rulesLayout->addStretch();
}

void ComposerPage::promptAddRule()
{
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, u"Add Rule"_s, u"Name:"_s, QLineEdit::Normal, QString(), &ok)
            .trimmed();
    if (!ok || name.isEmpty()) return;

    Rule rule;
    rule.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    rule.name = name;
    rule.enabled = true;
    rule.action.type = ActionType::Skip;

    m_data->ruleFile.rules.append(rule);
    report(m_data->saveRules());
    rebuildRulesSidebar();
    refresh();

    emit statusMessage(
        u"Added rule \"%1\" - edit rules.fct to define its match."_s.arg(name));
}

// ---- Variables

void ComposerPage::rebuildVarsSidebar()
{
    clearLayout(m_varsLayout);

    auto persistAndRefresh = [this]() {
        report(m_data->saveVariables());
        rebuildVarsSidebar();
        refresh();
    };

    Variables& vars = m_data->varsFile.vars;
    const QList<Variable> all = vars.all();

    for (int i = 0; i < int(all.size()); ++i) {
        auto* row = new QWidget;
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(6);

        auto* nameLabel = new QLabel(u"$"_s + all[i].name + u"$"_s);
        nameLabel->setObjectName(u"ComposerVarName"_s);
        nameLabel->setFixedWidth(70);

        auto* edit = new QLineEdit(all[i].value);
        edit->setObjectName(u"ComposerVarEdit"_s);
        edit->setPlaceholderText(u"(empty)"_s);

        const QString name = all[i].name;
        connect(edit, &QLineEdit::editingFinished, this, [this, name, edit]() {
            Variables& live = m_data->varsFile.vars;
            const QString next = edit->text().trimmed();
            if (live.value(name) == next) return;

            live.set(name, next);
            report(m_data->saveVariables());
            refresh();
        });

        QPushButton* deleteBtn = argDeleteButton(u"Remove variable"_s);
        connect(deleteBtn, &QPushButton::clicked, this, [this, name, persistAndRefresh]() {
            const QString token = u"$"_s + name + u"$"_s;
            int usage = 0;
            for (const QString& tag : m_store->doc().activeTags)
                if (tag.contains(token)) ++usage;

            if (!m_data->varsFile.vars.remove(name)) return;
            persistAndRefresh();

            if (usage > 0)
                emit statusMessage(u"Removed %1 - still referenced by %2 tag%3."_s.arg(token)
                                       .arg(usage)
                                       .arg(usage == 1 ? QString() : u"s"_s));
        });

        rowLayout->addWidget(nameLabel);
        rowLayout->addWidget(edit, 1);
        rowLayout->addWidget(deleteBtn);
        m_varsLayout->addWidget(row);
    }

    // Commits on Enter only; editingFinished would fire when tabbing between fields.
    auto* addRow = new QWidget;
    auto* addLayout = new QHBoxLayout(addRow);
    addLayout->setContentsMargins(0, 0, 0, 0);
    addLayout->setSpacing(6);

    auto* addName = new QLineEdit;
    addName->setObjectName(u"ComposerVarEdit"_s);
    addName->setPlaceholderText(u"name"_s);
    addName->setFixedWidth(70);

    auto* addValue = new QLineEdit;
    addValue->setObjectName(u"ComposerVarEdit"_s);
    addValue->setPlaceholderText(u"value"_s);

    auto commit = [this, addName, addValue, persistAndRefresh]() {
        const QString name = addName->text().trimmed();
        if (name.isEmpty()) return;

        // Duplicate name: clear and refocus.
        if (m_data->varsFile.vars.isDefined(name)) {
            addName->clear();
            addValue->clear();
            addName->setFocus();
            return;
        }

        m_data->varsFile.vars.set(name, addValue->text().trimmed());
        persistAndRefresh();
    };
    connect(addName, &QLineEdit::returnPressed, this, commit);
    connect(addValue, &QLineEdit::returnPressed, this, commit);

    addLayout->addWidget(addName);
    addLayout->addWidget(addValue, 1);
    m_varsLayout->addWidget(addRow);
    m_varsLayout->addStretch();
}

// ---- Workflows

void ComposerPage::rebuildWorkflowList()
{
    if (!m_workflowList) return;

    const QSignalBlocker block(m_workflowList);
    m_workflowList->clear();

    QList<Workflow>& workflows = m_data->workflows.workflows;
    if (workflows.isEmpty()) {
        auto* hint = new QListWidgetItem(u"Drop .json files here"_s);
        hint->setFlags(Qt::NoItemFlags);

        QFont font = hint->font();
        font.setItalic(true);
        hint->setFont(font);
        hint->setForeground(QColor(0x2a, 0x2a, 0x2a));
        m_workflowList->addItem(hint);
        return;
    }

    const QString filter = m_workflowFilter ? m_workflowFilter->text().trimmed() : QString();
    for (int i = 0; i < int(workflows.size()); ++i) {
        const Workflow& workflow = workflows[i];
        if (!filter.isEmpty() && !workflow.name.contains(filter, Qt::CaseInsensitive)) continue;

        auto* item = new QListWidgetItem(workflow.name);
        item->setData(Qt::UserRole, i);
        item->setToolTip(workflow.path);
        if (i == m_data->workflows.selectedIndex)
            item->setForeground(QColor(0x5a, 0x9a, 0x5a));
        m_workflowList->addItem(item);
    }

    // Wire once; the list persists across rebuilds.
    if (m_workflowList->property("_wired").toBool()) return;
    m_workflowList->setProperty("_wired", true);

    connect(m_workflowList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (item->data(Qt::UserRole).isNull()) return;
        const int index = item->data(Qt::UserRole).toInt();
        if (index == m_data->workflows.selectedIndex) return;

        m_data->workflows.selectedIndex = index;
        report(m_data->saveWorkflows());
        rebuildWorkflowList();
        emit workflowVarsChanged();
    });

    connect(m_workflowList, &WorkflowDropList::fileDropped, this, [this](const QString& path) {
        const QFileInfo info(path);

        // Copy into data/workflows so the stored path is relative to the data dir.
        const QString workflowsDir = m_data->dataDir() + u"/workflows"_s;
        QDir().mkpath(workflowsDir);

        const QString sourceCanonical = info.canonicalFilePath();
        const QString destCanonical = QFileInfo(workflowsDir).canonicalFilePath();
        const bool alreadyThere = !destCanonical.isEmpty() && !sourceCanonical.isEmpty()
            && sourceCanonical.startsWith(destCanonical + u"/"_s, Qt::CaseInsensitive);

        QString destination;
        if (alreadyThere) {
            destination = sourceCanonical;
        } else {
            // A name collision gets a " (N)" suffix rather than clobbering.
            destination = workflowsDir + u"/"_s + info.fileName();
            for (int n = 2; QFile::exists(destination); ++n)
                destination = u"%1/%2 (%3).%4"_s.arg(workflowsDir, info.completeBaseName())
                                  .arg(n)
                                  .arg(info.suffix());
            if (!QFile::copy(path, destination)) {
                emit statusMessage(u"Could not copy %1"_s.arg(info.fileName()));
                return;
            }
        }

        const QString relative = QDir(m_data->dataDir()).relativeFilePath(destination);
        for (const Workflow& existing : m_data->workflows.workflows)
            if (existing.path == relative) return;

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        Workflow workflow;
        workflow.id = QString::number(now);
        workflow.name = info.completeBaseName();
        workflow.path = relative;
        workflow.createdAt = now;

        // Newest on top; shift the selection with it.
        m_data->workflows.workflows.prepend(workflow);
        if (m_data->workflows.selectedIndex < 0)
            m_data->workflows.selectedIndex = 0;
        else
            ++m_data->workflows.selectedIndex;

        report(m_data->saveWorkflows());
        rebuildWorkflowList();
    });

    connect(m_workflowList, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
                QListWidgetItem* item = m_workflowList->itemAt(pos);
                if (!item || item->data(Qt::UserRole).isNull()) return;

                const int index = item->data(Qt::UserRole).toInt();
                if (index < 0 || index >= m_data->workflows.workflows.size()) return;

                QMenu menu(this);
                menu.addAction(u"Open file"_s, this, [this, index]() {
                    if (index >= m_data->workflows.workflows.size()) return;
                    openSystemFile(
                        m_data->workflowPath(m_data->workflows.workflows[index]));
                });
                menu.addSeparator();
                menu.addAction(u"Rename"_s, this, [this, index]() {
                    if (index >= m_data->workflows.workflows.size()) return;

                    bool ok = false;
                    const QString name = QInputDialog::getText(
                        this, u"Rename Workflow"_s, u"Name:"_s, QLineEdit::Normal,
                        m_data->workflows.workflows[index].name, &ok);
                    if (!ok || name.trimmed().isEmpty()) return;

                    m_data->workflows.workflows[index].name = name.trimmed();
                    report(m_data->saveWorkflows());
                    rebuildWorkflowList();
                });
                menu.addAction(u"Remove"_s, this, [this, index]() {
                    if (index >= m_data->workflows.workflows.size()) return;

                    m_data->workflows.workflows.removeAt(index);
                    const qsizetype count = m_data->workflows.workflows.size();
                    if (count == 0)
                        m_data->workflows.selectedIndex = -1;
                    else if (m_data->workflows.selectedIndex >= count)
                        m_data->workflows.selectedIndex = int(count) - 1;

                    report(m_data->saveWorkflows());
                    rebuildWorkflowList();
                    emit workflowVarsChanged();
                });
                menu.exec(m_workflowList->mapToGlobal(pos));
            });
}

} // namespace tc
