// Saved-state management (named user presets) for PromptComposerPage.
// Distinct from session save/restore: explicit named snapshots with
// optional preview thumbnails, hover-previewed in the states sidebar.

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

    const QString filter = m_statesFilter ? m_statesFilter->text().trimmed() : QString();
    for (int i = 0; i < states.size(); ++i) {
        const auto& state = states[i];
        if (!filter.isEmpty() && !state.name.contains(filter, Qt::CaseInsensitive)) continue;
        const bool hasImg =
            !state.previewImagePath.isEmpty() && QFile::exists(state.previewImagePath);
        auto* item = new QListWidgetItem((hasImg ? "◆  " : "") + state.name);
        // UserRole = model index; the visual row may differ when filtered.
        item->setData(Qt::UserRole, i);
        m_statesList->addItem(item);
    }
}

void PromptComposerPage::captureCurrentState(core::SavedState& state) const
{
    state.activeTags = m_activeTags;
    state.tagWeights.clear();
    for (auto it = m_tagWeights.cbegin(); it != m_tagWeights.cend(); ++it)
        if (qAbs(it.value() - 1.0f) >= 0.001f) state.tagWeights[it.key()] = it.value();
    state.deactivatedTags = m_deactivatedTags;

    // Convert runtime (entryId, imageIdx) keys to stable (uuid, imageFileName).
    state.activePushes = dumpActivePushes();

    state.ruleStates.clear();
    state.ruleArguments.clear();
    for (const auto& rule : m_rules->rules()) {
        state.ruleStates[rule.name] = rule.enabled;
        // Add/Replace args are user-typed in the rules sidebar; part of state.
        state.ruleArguments[rule.name] = rule.action.arguments;
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

    // Preserve identity (id keeps the on-disk dir) and the existing preview;
    // a new save would have neither, but overwrite is meant to refresh the
    // payload only.
    core::SavedState& state = m_stateManager.states()[row];
    const QString id = state.id;
    const QString name = state.name;
    const QString previewImagePath = state.previewImagePath;

    state = core::SavedState();
    state.id = id;
    state.name = name;
    state.previewImagePath = previewImagePath;
    captureCurrentState(state);

    m_stateManager.saveToDir(m_statesDir);
    rebuildStatesList();
    emit statusMessageRequested(QString("Overwrote: %1").arg(state.name));
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

    // Restore enabled flags and Add/Replace args, then persist. Rules
    // added since the snapshot stay defined but force-disabled so a
    // reload-from-disk reproduces what the user sees now. Match
    // expressions and force flags are left alone.
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

    // State is canonical; replace the var set wholesale and persist.
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

    // Workflow vars: replace wholesale, preserving the saved order.
    // Skipped when the workflow id is missing or empty so we don't blow
    // away the live workflow's vars with another workflow's snapshot
    // (or with an empty array from a no-workflow-selected save).
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

            // Warn and clear when a saved Image var references a missing
            // cache entry; better to re-pick than send a broken upload.
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

void PromptComposerPage::showStatePreview(int listRow)
{
    QListWidgetItem* item = m_statesList ? m_statesList->item(listRow) : nullptr;
    if (!item) {
        hideStatePreview();
        return;
    }
    const QVariant v = item->data(Qt::UserRole);
    if (!v.isValid()) {
        hideStatePreview();
        return;
    }
    const int row = v.toInt();
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

    const QRect itemRect = m_statesList->visualRect(m_statesList->model()->index(listRow, 0));
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
