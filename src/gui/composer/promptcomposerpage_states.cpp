// PromptComposerPage - saved-state management (named user presets).
// Distinct from session save/restore: states are explicit, named snapshots
// with optional preview thumbnails, hover-previewed in the states sidebar.

#include <gui/composer/promptcomposerpage.h>
#include <gui/composer/stateslistwidget.h>
#include <core/entrymodel.h>
#include <utils/appconfig.h>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPixmap>
#include <QScreen>

using namespace core;
using namespace utils;

namespace gui {

void PromptComposerPage::setStatesDir(const QString& dir)
{
    m_statesDir = dir;
    QDir().mkpath(dir);
    m_stateManager = core::StateManager::loadFromDir(dir);
    rebuildStatesList();
}

void PromptComposerPage::rebuildStatesList()
{
    if (!m_statesList) return;
    m_statesList->clear();

    const auto& states = m_stateManager.states();
    if (states.isEmpty()) {
        auto* item = new QListWidgetItem("No saved states");
        item->setFlags(Qt::NoItemFlags);
        QFont f = item->font();
        f.setItalic(true);
        item->setFont(f);
        item->setForeground(QColor("#2a2a2a"));
        m_statesList->addItem(item);
        return;
    }

    for (const auto& state : states) {
        const bool hasImg =
            !state.previewImagePath.isEmpty() && QFile::exists(state.previewImagePath);
        auto* item = new QListWidgetItem((hasImg ? "◆  " : "") + state.name);
        m_statesList->addItem(item);
    }
}

void PromptComposerPage::saveCurrentState()
{
    if (m_statesDir.isEmpty()) return;

    bool ok;
    const QString defaultName = QString("State %1").arg(m_stateManager.states().size() + 1);
    const QString name =
        QInputDialog::getText(this, "Save State", "Name:", QLineEdit::Normal, defaultName, &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    core::SavedState state;
    state.id = QString::number(QDateTime::currentMSecsSinceEpoch());
    state.name = name.trimmed();
    state.activeTags = m_activeTags;
    for (auto it = m_tagWeights.cbegin(); it != m_tagWeights.cend(); ++it)
        if (qAbs(it.value() - 1.0f) >= 0.001f) state.tagWeights[it.key()] = it.value();
    state.deactivatedTags = m_deactivatedTags;

    // Convert runtime (entryId, imageIdx) keys to stable (uuid, imageFileName).
    state.activePushes = dumpActivePushes();

    for (const auto& rule : m_rules->rules()) {
        state.ruleStates[rule.name] = rule.enabled;
        // Capture Add/Replace args too - these are what the user types in the
        // rules sidebar arg-edit and they're part of the prompt configuration.
        state.ruleArguments[rule.name] = rule.action.arguments;
    }

    if (m_varIndex)
        for (const auto& var : m_varIndex->variables())
            state.varValues.append({var.name, var.value});

    if (m_wfManager) {
        const core::WorkflowFile* wf = m_wfManager->selectedFile();
        state.selectedWorkflowId = wf ? wf->id : QString();

        QJsonArray varValues;
        for (const auto& var : m_wfManager->variables())
            varValues.append(core::WorkflowManager::varToJson(var));
        state.workflowVarValues = varValues;
    }

    state.activeLoraUuids = m_activeLoraUuids;

    m_stateManager.states() << state;
    m_stateManager.saveToDir(m_statesDir);
    rebuildStatesList();
    emit statusMessageRequested(QString("Saved: %1").arg(state.name));
}

void PromptComposerPage::restoreState(const core::SavedState& state)
{
    m_activeTags.clear();
    m_activeTagSet.clear();
    m_tagWeights.clear();
    m_deactivatedTags.clear();
    // m_activePushes is replaced wholesale by loadActivePushes below.

    m_activeTags = state.activeTags;
    for (const auto& t : m_activeTags)
        m_activeTagSet.insert(t);
    m_tagWeights = state.tagWeights;
    m_deactivatedTags = state.deactivatedTags;

    const int missing = loadActivePushes(state.activePushes);

    m_activeLoraUuids = state.activeLoraUuids;
    emit loraUuidsRestored(m_activeLoraUuids);

    // Restore rule toggle states + arguments and persist to rules.fct.
    // - Rules that were in the saved state get their enabled flag and (for
    //   Add/Replace) action arguments restored.
    // - Rules added after the state was saved are kept in the file (they
    //   stay defined) but their enabled flag is forced to false, so a
    //   later reload-from-disk reproduces what the user sees right now.
    // - Match expressions, force flags, and arguments of rules absent from
    //   the state are preserved as-is.
    for (auto& rule : m_rules->rules()) {
        auto it = state.ruleStates.find(rule.name);
        if (it != state.ruleStates.end()) {
            rule.enabled = it.value();
            auto ait = state.ruleArguments.find(rule.name);
            if (ait != state.ruleArguments.end()) rule.action.arguments = ait.value();
        }
        else {
            rule.enabled = false;
        }
    }
    m_rules->saveToFile(BASE_PATH + "/" + RULES_PATH);
    m_suppressRuleSave = true;
    rebuildRulesSidebar();
    m_suppressRuleSave = false;

    // Variables: state is canonical. Fully replace the current var set -
    // any var only in the state is added, any var only in the current
    // session is dropped. Persists to vars.fct so it survives restart.
    if (m_varIndex) {
        QList<core::Variable> newVars;
        for (const auto& pair : state.varValues) {
            core::Variable v;
            v.name = pair.first;
            v.value = pair.second;
            newVars << v;
        }
        m_varIndex->variables() = newVars;
        m_varIndex->saveToFile(BASE_PATH + "/" + VARS_PATH);
        rebuildVarsSidebar();
    }

    // Restore workflow selection first (so var restore targets the right workflow)
    bool workflowMissing = false;
    if (m_wfManager && !state.selectedWorkflowId.isEmpty()) {
        const int wfIdx = m_wfManager->workflowIndexById(state.selectedWorkflowId);
        if (wfIdx >= 0) {
            m_wfManager->setSelectedIndex(wfIdx);
            rebuildWorkflowList();
        }
        else {
            workflowMissing = true;
        }
    }

    // Workflow variables: state is canonical for the selected workflow.
    // Fully replace the var list - vars only in the state are added, vars
    // only in the live workflow are dropped. Order from the saved array is
    // preserved. Skip when the saved workflow id no longer exists (would
    // blow away the *current* workflow's vars with a different workflow's
    // snapshot) and when the state was saved with no workflow selected at
    // all (its empty workflowVarValues would silently clear the currently-
    // selected workflow's vars).
    if (m_wfManager && !workflowMissing && !state.selectedWorkflowId.isEmpty()) {
        // Snapshot existing types for legacy states that didn't include a
        // "type" field (varFromJson defaults those to String otherwise).
        QHash<QString, core::WorkflowVarType> liveTypes;
        for (const auto& v : m_wfManager->variables())
            if (!v.placeholder.isEmpty()) liveTypes[v.placeholder] = v.type;

        QList<core::WorkflowVar> newVars;
        for (const QJsonValue& entry : state.workflowVarValues) {
            QJsonObject o = entry.toObject();
            // Backfill missing "type" from the live workflow before parsing.
            if (!o.contains("type")) {
                const QString ph = o["placeholder"].toString();
                if (liveTypes.contains(ph))
                    o["type"] = core::WorkflowManager::typeToStr(liveTypes[ph]);
            }
            core::WorkflowVar v = core::WorkflowManager::varFromJson(o);

            // State-restore-specific: warn if an Image var references a
            // cache entry that no longer exists, then clear so the user
            // re-picks rather than silently sending a broken upload.
            if (v.type == core::WorkflowVarType::Image && !v.imageUuid.isEmpty() && m_inputCache &&
                !m_inputCache->has(v.imageUuid)) {
                emit statusMessageRequested(
                    QString("Image input %1 missing from cache (%2) - repick")
                        .arg(v.placeholder, v.imageUuid.left(8)));
                v.imageUuid.clear();
            }
            newVars << v;
        }
        m_wfManager->variables() = newVars;
    }

    if (m_wfManager) {
        m_wfManager->saveToFile(m_wfSavePath);
        emit workflowVarsChanged();
    }

    repush();

    QStringList warnings;
    if (workflowMissing) warnings << "workflow no longer exists";
    if (missing > 0)
        warnings
            << QString("%1 entr%2 no longer exist").arg(missing).arg(missing == 1 ? "y" : "ies");

    if (warnings.isEmpty())
        emit statusMessageRequested(QString("Restored: %1").arg(state.name));
    else
        emit statusMessageRequested(
            QString("Restored: %1  (%2)").arg(state.name, warnings.join(", ")));
}

void PromptComposerPage::showStatePreview(int row)
{
    if (row < 0 || row >= m_stateManager.states().size()) {
        hideStatePreview();
        return;
    }
    const core::SavedState& state = m_stateManager.states()[row];
    if (state.previewImagePath.isEmpty() || !QFile::exists(state.previewImagePath)) {
        hideStatePreview();
        return;
    }
    QPixmap pix(state.previewImagePath);
    if (pix.isNull()) {
        hideStatePreview();
        return;
    }

    pix = pix.scaled(220, 220, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_statesPreviewPopup->setPixmap(pix);
    m_statesPreviewPopup->adjustSize();

    const QRect itemRect = m_statesList->visualRect(m_statesList->model()->index(row, 0));
    const QPoint globalTopLeft = m_statesList->viewport()->mapToGlobal(itemRect.topLeft());
    int x = globalTopLeft.x() - m_statesPreviewPopup->width() - 8;
    int y = globalTopLeft.y();

    if (QScreen* scr = QGuiApplication::screenAt(globalTopLeft)) {
        const QRect sg = scr->availableGeometry();
        y = qBound(sg.top(), y, sg.bottom() - m_statesPreviewPopup->height());
    }

    m_statesPreviewPopup->move(x, y);
    m_statesPreviewPopup->show();
    m_statesPreviewPopup->raise();
}

void PromptComposerPage::hideStatePreview()
{
    if (m_statesPreviewPopup) m_statesPreviewPopup->hide();
}

} // namespace gui
