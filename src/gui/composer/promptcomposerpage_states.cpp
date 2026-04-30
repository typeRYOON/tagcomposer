// PromptComposerPage — saved-state management (named user presets).
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

namespace {

// String <-> enum for the workflow var type field in saved-state JSON.
// Stored explicitly so the JSON is self-describing and survives type
// changes in the live workflow definition.
QString wfVarTypeToString(core::WorkflowVarType t)
{
    switch (t) {
    case core::WorkflowVarType::Seed:       return "Seed";
    case core::WorkflowVarType::String:     return "String";
    case core::WorkflowVarType::Integer:    return "Integer";
    case core::WorkflowVarType::Float:      return "Float";
    case core::WorkflowVarType::DirSearch:  return "DirSearch";
    case core::WorkflowVarType::LatentSize: return "LatentSize";
    }
    return "String";
}

core::WorkflowVarType wfVarTypeFromString(const QString& s)
{
    if (s == "Seed")       return core::WorkflowVarType::Seed;
    if (s == "Integer")    return core::WorkflowVarType::Integer;
    if (s == "Float")      return core::WorkflowVarType::Float;
    if (s == "DirSearch")  return core::WorkflowVarType::DirSearch;
    if (s == "LatentSize") return core::WorkflowVarType::LatentSize;
    return core::WorkflowVarType::String;
}

} // anonymous namespace

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
        QFont f = item->font(); f.setItalic(true);
        item->setFont(f);
        item->setForeground(QColor("#2a2a2a"));
        m_statesList->addItem(item);
        return;
    }

    for (const auto& state : states) {
        const bool hasImg = !state.previewImagePath.isEmpty()
                         && QFile::exists(state.previewImagePath);
        auto* item = new QListWidgetItem((hasImg ? "◆  " : "") + state.name);
        m_statesList->addItem(item);
    }
}

void PromptComposerPage::saveCurrentState()
{
    if (m_statesDir.isEmpty()) return;

    bool ok;
    const QString defaultName =
        QString("State %1").arg(m_stateManager.states().size() + 1);
    const QString name = QInputDialog::getText(
        this, "Save State", "Name:", QLineEdit::Normal, defaultName, &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    core::SavedState state;
    state.id              = QString::number(QDateTime::currentMSecsSinceEpoch());
    state.name            = name.trimmed();
    state.activeTags      = m_activeTags;
    for (auto it = m_tagWeights.cbegin(); it != m_tagWeights.cend(); ++it)
        if (qAbs(it.value() - 1.0f) >= 0.001f)
            state.tagWeights[it.key()] = it.value();
    state.deactivatedTags = m_deactivatedTags;

    // Convert runtime (entryId, imageIdx) keys to stable (uuid, imageFileName)
    for (auto it = m_activePushes.cbegin(); it != m_activePushes.cend(); ++it) {
        const int runtimeId = int(quint32(it.key() >> 32));
        const int imageIdx  = int(quint32(it.key() & 0xFFFFFFFFLL));
        core::Entry* entry  = m_entryModel ? m_entryModel->entryById(runtimeId) : nullptr;
        if (!entry || imageIdx >= entry->images.size()) continue;
        core::EntryPush ep;
        ep.uuid          = entry->uuid;
        ep.imageFileName = entry->images[imageIdx].fileName;
        ep.tags          = it.value();
        state.activePushes << ep;
    }

    for (const auto& rule : m_rules->rules()) {
        state.ruleStates[rule.name]    = rule.enabled;
        // Capture Add/Replace args too — these are what the user types in the
        // rules sidebar arg-edit and they're part of the prompt configuration.
        state.ruleArguments[rule.name] = rule.action.arguments;
    }

    if (m_varIndex)
        for (const auto& var : m_varIndex->variables())
            state.varValues[var.name] = var.value;

    if (m_wfManager) {
        const core::WorkflowFile* wf = m_wfManager->selectedFile();
        state.selectedWorkflowId = wf ? wf->id : QString();

        QJsonArray varValues;
        for (const auto& var : m_wfManager->variables()) {
            if (var.placeholder.isEmpty()) continue;
            QJsonObject o;
            o["placeholder"] = var.placeholder;
            o["type"]        = wfVarTypeToString(var.type);
            switch (var.type) {
            case core::WorkflowVarType::Seed:
                o["seedBehavior"] = int(var.seedBehavior);
                o["seedValue"]    = var.seedValue;   // integer, not double
                break;
            case core::WorkflowVarType::String:
            case core::WorkflowVarType::LatentSize:
                o["stringValue"] = var.stringValue;
                break;
            case core::WorkflowVarType::Integer:
                o["intValue"] = var.intValue;
                break;
            case core::WorkflowVarType::Float:
                o["floatValue"] = var.floatValue;
                break;
            case core::WorkflowVarType::DirSearch:
                o["searchDir"]       = var.searchDir;
                o["selectedFile"]    = var.selectedFile;
                o["extensionFilter"] = var.extensionFilter;
                break;
            }
            varValues.append(o);
        }
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
    m_activePushes.clear();

    m_activeTags      = state.activeTags;
    for (const auto& t : m_activeTags) m_activeTagSet.insert(t);
    m_tagWeights      = state.tagWeights;
    m_deactivatedTags = state.deactivatedTags;

    // Resolve uuid+imageFileName back to runtime keys; count entries that no longer exist
    int missing = 0;
    for (const auto& ep : state.activePushes) {
        core::Entry* entry = m_entryModel ? m_entryModel->entryByUuid(ep.uuid) : nullptr;
        if (!entry) { ++missing; continue; }
        int imageIdx = -1;
        for (int i = 0; i < entry->images.size(); ++i) {
            if (entry->images[i].fileName == ep.imageFileName) { imageIdx = i; break; }
        }
        if (imageIdx < 0) { ++missing; continue; }
        const qint64 key = (qint64(entry->id) << 32) | quint32(imageIdx);
        m_activePushes[key] = ep.tags;
    }

    QMap<int, QList<int>> activeGroups;
    for (auto it = m_activePushes.cbegin(); it != m_activePushes.cend(); ++it) {
        activeGroups[int(quint32(it.key() >> 32))].append(
            int(quint32(it.key() & 0xFFFFFFFFLL)));
    }
    emit activeGroupsChanged(activeGroups);

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
            if (ait != state.ruleArguments.end())
                rule.action.arguments = ait.value();
        } else {
            rule.enabled = false;
        }
    }
    m_rules->saveToFile(BASE_PATH + "/" + RULES_PATH);
    m_suppressRuleSave = true;
    rebuildRulesSidebar();
    m_suppressRuleSave = false;

    // Variables: state is canonical. Fully replace the current var set —
    // any var only in the state is added, any var only in the current
    // session is dropped. Persists to vars.fct so it survives restart.
    if (m_varIndex) {
        QList<core::Variable> newVars;
        for (auto it = state.varValues.cbegin(); it != state.varValues.cend(); ++it) {
            core::Variable v;
            v.name  = it.key();
            v.value = it.value();
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
        } else {
            workflowMissing = true;
        }
    }

    // Workflow variables: state is canonical for the selected workflow.
    // Fully replace the var list — vars only in the state are added, vars
    // only in the live workflow are dropped. Order from the saved array is
    // preserved. Skip when the saved workflow id no longer exists, since
    // blowing away the *current* workflow's vars with a different workflow's
    // snapshot would be destructive.
    if (m_wfManager && !workflowMissing) {
        // Snapshot existing types for backward compat with old states that
        // didn't include a "type" field.
        QHash<QString, core::WorkflowVarType> liveTypes;
        for (const auto& v : m_wfManager->variables())
            liveTypes[v.placeholder] = v.type;

        QList<core::WorkflowVar> newVars;
        for (const QJsonValue& entry : state.workflowVarValues) {
            const QJsonObject o = entry.toObject();
            const QString placeholder = o["placeholder"].toString();
            if (placeholder.isEmpty()) continue;

            core::WorkflowVar v;
            v.placeholder = placeholder;
            if (o.contains("type")) {
                v.type = wfVarTypeFromString(o["type"].toString());
            } else if (liveTypes.contains(placeholder)) {
                v.type = liveTypes[placeholder];
            } else {
                v.type = core::WorkflowVarType::String;
            }
            switch (v.type) {
            case core::WorkflowVarType::Seed:
                v.seedBehavior = core::SeedBehavior(o["seedBehavior"].toInt(0));
                v.seedValue    = o["seedValue"].toInteger(0);
                break;
            case core::WorkflowVarType::String:
            case core::WorkflowVarType::LatentSize:
                v.stringValue = o["stringValue"].toString();
                break;
            case core::WorkflowVarType::Integer:
                v.intValue = o["intValue"].toInt();
                break;
            case core::WorkflowVarType::Float:
                v.floatValue = o["floatValue"].toDouble();
                break;
            case core::WorkflowVarType::DirSearch:
                v.searchDir       = o["searchDir"].toString();
                v.selectedFile    = o["selectedFile"].toString();
                v.extensionFilter = o["extensionFilter"].toString();
                break;
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
    if (workflowMissing)
        warnings << "workflow no longer exists";
    if (missing > 0)
        warnings << QString("%1 entr%2 no longer exist").arg(missing).arg(missing == 1 ? "y" : "ies");

    if (warnings.isEmpty())
        emit statusMessageRequested(QString("Restored: %1").arg(state.name));
    else
        emit statusMessageRequested(
            QString("Restored: %1  (%2)").arg(state.name, warnings.join(", ")));
}

void PromptComposerPage::showStatePreview(int row)
{
    if (row < 0 || row >= m_stateManager.states().size()) {
        hideStatePreview(); return;
    }
    const core::SavedState& state = m_stateManager.states()[row];
    if (state.previewImagePath.isEmpty() || !QFile::exists(state.previewImagePath)) {
        hideStatePreview(); return;
    }
    QPixmap pix(state.previewImagePath);
    if (pix.isNull()) { hideStatePreview(); return; }

    pix = pix.scaled(220, 220, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_statesPreviewPopup->setPixmap(pix);
    m_statesPreviewPopup->adjustSize();

    const QRect itemRect = m_statesList->visualRect(
        m_statesList->model()->index(row, 0));
    const QPoint globalTopLeft =
        m_statesList->viewport()->mapToGlobal(itemRect.topLeft());
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
