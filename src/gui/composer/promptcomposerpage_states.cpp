// Named saved states (user presets). Distinct from session save/restore:
// explicit named snapshots with optional preview thumbnails.

#include <gui/composer/promptcomposerpage.h>
#include <gui/composer/statesgridview.h>
#include <core/entrymodel.h>
#include <utils/appconfig.h>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QSet>

using namespace core;
using namespace utils;

namespace gui {

void PromptComposerPage::setStatesDir(const QString& dir)
{
    m_statesDir = dir;
    QDir().mkpath(dir);
    m_stateManager = core::StateManager::loadFromDir(dir);
    // Re-point the grid at the (potentially) re-initialized states list.
    if (m_statesGrid) m_statesGrid->setStates(&m_stateManager.states());
    rebuildStatesList();
}

void PromptComposerPage::rebuildStatesList()
{
    if (!m_statesGrid) return;

    const QString filter = m_statesFilter ? m_statesFilter->text().trimmed() : QString();
    m_statesGrid->setStates(&m_stateManager.states());
    m_statesGrid->setFilter(filter);

    if (m_statesEmptyHint) {
        const bool empty = m_statesGrid->isEmpty();
        m_statesEmptyHint->setText(m_stateManager.states().isEmpty()
                                       ? "No saved states"
                                       : "No states match filter");
        m_statesEmptyHint->setVisible(empty);
        m_statesGrid->setVisible(!empty);
    }
}

void PromptComposerPage::captureCurrentState(core::SavedState& state) const
{
    state.activeTags = m_activeTags;
    state.tagWeights.clear();
    for (auto it = m_tagWeights.cbegin(); it != m_tagWeights.cend(); ++it)
        if (qAbs(it.value() - 1.0f) >= 0.001f) state.tagWeights[it.key()] = it.value();
    state.deactivatedTags = m_deactivatedTags;
    state.deactivatedCategory = m_deactivatedCategory;

    // Convert runtime keys to stable (uuid, imageFileName).
    state.activePushes = dumpActivePushes();

    state.ruleStates.clear();
    state.ruleArguments.clear();
    state.rulesSnapshot = QJsonArray();
    for (const auto& rule : m_rules->rules()) {
        state.ruleStates[rule.uuid] = rule.enabled;
        // Add/Replace args are user-typed; part of state.
        state.ruleArguments[rule.uuid] = rule.action.arguments;
        // Full definition so restoring on a machine missing this rule can
        // recreate it locally.
        state.rulesSnapshot.append(core::RuleEngine::ruleToJson(rule));
    }

    state.varValues.clear();
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
    else {
        state.selectedWorkflowId.clear();
        state.workflowVarValues = QJsonArray();
    }

    state.activeLoraUuids = m_activeLoraUuids;
}

core::SavedState PromptComposerPage::currentSnapshot() const
{
    core::SavedState state;
    captureCurrentState(state);
    return state;
}

void PromptComposerPage::restoreFromSnapshot(const core::SavedState& s)
{
    restoreState(s);
}

void PromptComposerPage::appendSnapshotAsState(core::SavedState state, const QString& displayName)
{
    if (m_statesDir.isEmpty()) return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    state.id = QString::number(now);
    state.createdAt = now;
    state.name = displayName.trimmed().isEmpty()
                     ? QString("State %1").arg(m_stateManager.states().size() + 1)
                     : displayName.trimmed();

    m_stateManager.states().prepend(state);
    m_stateManager.saveToDir(m_statesDir);
    rebuildStatesList();
    emit statusMessageRequested(QString("Saved: %1").arg(state.name));
}

QList<core::WorkflowVar> PromptComposerPage::imageVarsFromStates() const
{
    QList<core::WorkflowVar> out;
    for (const core::SavedState& s : m_stateManager.states()) {
        for (const QJsonValue& entry : s.workflowVarValues) {
            core::WorkflowVar v = core::WorkflowManager::varFromJson(entry.toObject());
            if (v.type == core::WorkflowVarType::Image) out << v;
        }
    }
    return out;
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
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    state.id = QString::number(now);
    state.createdAt = now;
    state.name = name.trimmed();
    captureCurrentState(state);

    m_stateManager.states().prepend(state);
    m_stateManager.saveToDir(m_statesDir);
    rebuildStatesList();
    emit statusMessageRequested(QString("Saved: %1").arg(state.name));
}

void PromptComposerPage::overwriteState(int row)
{
    if (m_statesDir.isEmpty()) return;
    if (row < 0 || row >= m_stateManager.states().size()) return;

    // Preserve id (on-disk dir) and existing preview; overwrite refreshes
    // payload only. Bump createdAt so MRU floats to top.
    const QString id = m_stateManager.states()[row].id;
    const QString name = m_stateManager.states()[row].name;
    const QString previewImagePath = m_stateManager.states()[row].previewImagePath;

    core::SavedState fresh;
    fresh.id = id;
    fresh.name = name;
    fresh.createdAt = QDateTime::currentMSecsSinceEpoch();
    fresh.previewImagePath = previewImagePath;
    captureCurrentState(fresh);

    m_stateManager.states()[row] = fresh;
    if (row != 0) m_stateManager.states().move(row, 0);

    m_stateManager.saveToDir(m_statesDir);
    rebuildStatesList();
    emit statusMessageRequested(QString("Overwrote: %1").arg(name));
}

void PromptComposerPage::restoreState(const core::SavedState& state)
{
    m_activeTags.clear();
    m_activeTagSet.clear();
    m_tagWeights.clear();
    m_deactivatedTags.clear();
    m_deactivatedCategory.clear();
    // m_activePushes is replaced wholesale by loadActivePushes below.

    m_activeTags = state.activeTags;
    for (const auto& t : m_activeTags)
        m_activeTagSet.insert(t);
    m_tagWeights = state.tagWeights;
    m_deactivatedTags = state.deactivatedTags;
    m_deactivatedCategory = state.deactivatedCategory;

    const int missing = loadActivePushes(state.activePushes);

    m_activeLoraUuids = state.activeLoraUuids;
    emit loraUuidsRestored(m_activeLoraUuids);

    // Bring back any rules the state knows about but the local rules.fct
    // doesn't (match by uuid). Existing-uuid rules are left untouched - the
    // local definition wins.
    int rulesAdded = 0;
    {
        QSet<QString> existing;
        for (const auto& r : m_rules->rules())
            existing.insert(r.uuid);
        for (const QJsonValue& v : state.rulesSnapshot) {
            const core::Rule restored = core::RuleEngine::ruleFromJson(v.toObject());
            if (restored.uuid.isEmpty()) continue;
            if (existing.contains(restored.uuid)) continue;
            m_rules->rules().append(restored);
            existing.insert(restored.uuid);
            ++rulesAdded;
        }
    }

    // Restore enabled flags + Add/Replace args. Rules added after the
    // snapshot are force-disabled so reload-from-disk matches the view.
    // Match expressions and force flags are untouched.
    for (auto& rule : m_rules->rules()) {
        auto it = state.ruleStates.find(rule.uuid);
        if (it != state.ruleStates.end()) {
            rule.enabled = it.value();
            auto ait = state.ruleArguments.find(rule.uuid);
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

    // State is canonical; replace var set wholesale and persist.
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

    // Restore workflow selection first so var restore targets the right one.
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

    // Workflow vars: replace wholesale, preserving saved order. Skipped
    // when workflow id is missing/empty to avoid clobbering live vars with
    // another workflow's (or an empty no-selection) snapshot.
    if (m_wfManager && !workflowMissing && !state.selectedWorkflowId.isEmpty()) {
        // Snapshot live types for legacy states without a "type" field
        // (varFromJson would default to String).
        QHash<QString, core::WorkflowVarType> liveTypes;
        for (const auto& v : m_wfManager->variables())
            if (!v.placeholder.isEmpty()) liveTypes[v.placeholder] = v.type;

        QList<core::WorkflowVar> newVars;
        for (const QJsonValue& entry : state.workflowVarValues) {
            QJsonObject o = entry.toObject();
            if (!o.contains("type")) {
                const QString ph = o["placeholder"].toString();
                if (liveTypes.contains(ph))
                    o["type"] = core::WorkflowManager::typeToStr(liveTypes[ph]);
            }
            core::WorkflowVar v = core::WorkflowManager::varFromJson(o);

            // Clear missing-cache Image vars; better to re-pick than upload broken.
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
    if (rulesAdded > 0)
        warnings << QString("appended %1 rule%2 from state")
                        .arg(rulesAdded)
                        .arg(rulesAdded == 1 ? "" : "s");

    // Undo/redo are silent and manage stacks externally.
    if (m_suppressUndoCapture) return;

    if (warnings.isEmpty())
        emit statusMessageRequested(QString("Restored: %1").arg(state.name));
    else
        emit statusMessageRequested(
            QString("Restored: %1  (%2)").arg(state.name, warnings.join(", ")));

    rebaselineUndo();
}

} // namespace gui
