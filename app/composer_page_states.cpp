// Named saved states, the session file, and PNG drops carrying a baked state.
//
// A state is an explicit, named snapshot with an optional thumbnail; the
// session is the unnamed one written on close and read on startup.

#include <app/composer_page.h>
#include <app/app_data.h>
#include <app/paths.h>
#include <app/composer_widgets.h>
#include <app/states_grid_view.h>
#include <app/tag_search_bar.h>
#include <app/workflow_input_cache.h>
#include <core/png_text.h>
#include <core/rule_io.h>
#include <core/workflow_io.h>
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QImage>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

// A single local .png, else empty. A child widget with its own drop handling
// still wins: the page only sees a drag that no child accepted.
QString bakedPngFromMime(const QMimeData* mime)
{
    if (!mime || !mime->hasUrls()) return {};

    const QList<QUrl> urls = mime->urls();
    if (urls.size() != 1) return {};

    const QString path = urls.first().toLocalFile();
    return path.endsWith(u".png"_s, Qt::CaseInsensitive) ? path : QString();
}

} // namespace

// ---- The states view

void ComposerPage::setStatesViewActive(bool active)
{
    if (m_statesViewActive == active) return;
    m_statesViewActive = active;

    // Kept in step without re-entering this method.
    if (m_statesToggleBtn && m_statesToggleBtn->isChecked() != active) {
        const QSignalBlocker block(m_statesToggleBtn);
        m_statesToggleBtn->setChecked(active);
    }

    if (m_searchBar) m_searchBar->setVisible(!active);

    for (QWidget* widget : m_composerFloats) {
        if (!widget) continue;
        // The preview inset has its own lifecycle: it is only visible once an
        // image has actually arrived.
        if (active)
            widget->setVisible(false);
        else if (widget == m_previewLabel)
            widget->setVisible(!m_currentPixmap.isNull());
        else
            widget->setVisible(true);
    }

    if (active) {
        rebuildStatesList();
        if (m_statesFilter) m_statesFilter->setFocus();
    }

    // Snap-fade to the target view. A rebuild already in flight, from a tile
    // click into restoreState say, collapses into this one.
    if (m_mainStackFade->state() == QAbstractAnimation::Running) m_mainStackFade->stop();
    m_mainStack->setCurrentIndex(active ? 2 : composerStackIndex());

    QGraphicsOpacityEffect* effect = stackChildFx(m_mainStack->currentIndex());
    if (!effect) return;

    effect->setOpacity(0.0);
    m_mainStackFade->setTargetObject(effect);
    m_mainStackFade->setStartValue(0.0);
    m_mainStackFade->setEndValue(1.0);
    m_mainStackFade->start();
}

void ComposerPage::leaveStatesViewMode()
{
    if (!m_statesViewActive) return;
    m_statesViewActive = false;

    if (m_statesToggleBtn) {
        const QSignalBlocker block(m_statesToggleBtn);
        m_statesToggleBtn->setChecked(false);
    }
    if (m_searchBar) m_searchBar->setVisible(true);

    for (QWidget* widget : m_composerFloats) {
        if (!widget) continue;
        if (widget == m_previewLabel)
            widget->setVisible(!m_currentPixmap.isNull());
        else
            widget->setVisible(true);
    }
}

void ComposerPage::rebuildStatesList()
{
    if (!m_statesGrid) return;

    m_statesGrid->setStates(&m_stateManager.states());
    m_statesGrid->setFilter(m_statesFilter ? m_statesFilter->text().trimmed() : QString());

    if (!m_statesEmptyHint) return;

    const bool empty = m_statesGrid->isEmpty();
    m_statesEmptyHint->setText(m_stateManager.states().isEmpty() ? u"No saved states"_s
                                                                 : u"No states match filter"_s);
    m_statesEmptyHint->setVisible(empty);
    m_statesGrid->setVisible(!empty);
}

// ---- Capture

void ComposerPage::captureCurrentState(SavedState& state) const
{
    const ComposerDoc& doc = m_store->doc();

    state.activeTags = doc.activeTags;

    // A weight of 1 is the default, so storing it would only bloat the file.
    state.tagWeights.clear();
    for (auto it = doc.weights.cbegin(); it != doc.weights.cend(); ++it)
        if (qAbs(it.value() - 1.0f) >= 0.001f) state.tagWeights[it.key()] = it.value();

    state.deactivatedTags = doc.deactivated;
    state.deactivatedCategory = m_deactivatedCategory;
    state.activePushes = doc.pushes;
    state.customTagFacets = doc.customFacets;

    state.ruleStates.clear();
    state.ruleArguments.clear();
    state.rulesSnapshot = QJsonArray();
    for (const Rule& rule : m_data->ruleFile.rules) {
        state.ruleStates[rule.uuid] = rule.enabled;
        // The injected tags are user-typed, so they belong to the state.
        state.ruleArguments[rule.uuid] = rule.action.arguments;
        // The whole definition, so restoring on a machine that never had this
        // rule can recreate it locally.
        state.rulesSnapshot.append(ruleToJson(rule));
    }

    state.varValues.clear();
    for (const Variable& variable : m_data->varsFile.vars.all())
        state.varValues.append({variable.name, variable.value});

    if (const Workflow* workflow = selectedWorkflow()) {
        state.selectedWorkflowId = workflow->id;

        QJsonArray values;
        for (const WorkflowVar& var : workflow->vars)
            values.append(varToJson(var));
        state.workflowVarValues = values;
    } else {
        state.selectedWorkflowId.clear();
        state.workflowVarValues = QJsonArray();
    }

    state.activeLoraUuids.clear();
    for (const Lora& lora : doc.loraStack)
        state.activeLoraUuids << lora.sha256;

    // The stamp carries names for the UI plus the resolved order and format
    // list, so the state replays identically after its profile is edited or
    // deleted.
    state.profilesStamped = true;
    state.groupProfileName = m_profiles.activeGroup();
    state.groupOrder = m_groups.names();
    state.formatProfileName = m_profiles.activeFormat();
    state.facetFormats = m_facetFormats;
}

SavedState ComposerPage::currentSnapshot() const
{
    SavedState state;
    captureCurrentState(state);
    return state;
}

void ComposerPage::restoreFromSnapshot(const SavedState& state)
{
    restoreState(state);
}

void ComposerPage::appendSnapshotAsState(SavedState state, const QString& displayName)
{
    if (m_statesDir.isEmpty()) return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    state.id = QString::number(now);
    state.createdAt = now;
    state.name = displayName.trimmed().isEmpty()
        ? u"State %1"_s.arg(m_stateManager.states().size() + 1)
        : displayName.trimmed();

    m_stateManager.states().prepend(state);
    m_stateManager.saveToDir(m_statesDir);
    rebuildStatesList();
    emit statusMessage(u"Saved: %1"_s.arg(state.name));
}

QList<WorkflowVar> ComposerPage::imageVarsFromStates() const
{
    QList<WorkflowVar> out;
    for (const SavedState& state : m_stateManager.states()) {
        for (const QJsonValue entry : state.workflowVarValues) {
            const WorkflowVar var = varFromJson(entry.toObject());
            if (std::holds_alternative<ImageVar>(var.value)) out << var;
        }
    }
    return out;
}

void ComposerPage::saveCurrentState()
{
    if (m_statesDir.isEmpty()) return;

    bool ok = false;
    const QString name = QInputDialog::getText(
        this, u"Save State"_s, u"Name:"_s, QLineEdit::Normal,
        u"State %1"_s.arg(m_stateManager.states().size() + 1), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    SavedState state;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    state.id = QString::number(now);
    state.createdAt = now;
    state.name = name.trimmed();
    captureCurrentState(state);

    m_stateManager.states().prepend(state);
    m_stateManager.saveToDir(m_statesDir);
    rebuildStatesList();
    emit statusMessage(u"Saved: %1"_s.arg(state.name));
}

void ComposerPage::overwriteState(int row)
{
    if (m_statesDir.isEmpty()) return;
    if (row < 0 || row >= m_stateManager.states().size()) return;

    // The id is the folder on disk and the preview is a separate file, so
    // both are kept; only the payload is refreshed. createdAt is bumped so it
    // floats to the top.
    SavedState fresh;
    fresh.id = m_stateManager.states()[row].id;
    fresh.name = m_stateManager.states()[row].name;
    fresh.previewImagePath = m_stateManager.states()[row].previewImagePath;
    fresh.createdAt = QDateTime::currentMSecsSinceEpoch();
    captureCurrentState(fresh);

    m_stateManager.states()[row] = fresh;
    if (row != 0) m_stateManager.states().move(row, 0);

    m_stateManager.saveToDir(m_statesDir);
    rebuildStatesList();
    emit statusMessage(u"Overwrote: %1"_s.arg(fresh.name));
}

// ---- Restore

void ComposerPage::restoreState(const SavedState& state)
{
    ComposerDoc doc;
    doc.activeTags = state.activeTags;
    doc.weights = state.tagWeights;
    doc.deactivated = state.deactivatedTags;
    doc.customFacets = state.customTagFacets;

    // A push naming an entry that no longer exists is dropped rather than
    // left claiming tags nothing can un-claim.
    int missingEntries = 0;
    for (const EntryPush& push : state.activePushes) {
        const Entry* entry = m_entries->find(push.entryUuid);
        bool haveImage = false;
        if (entry)
            for (const EntryImage& image : entry->images)
                if (image.fileName == push.imageFile) haveImage = true;

        if (!haveImage) {
            ++missingEntries;
            continue;
        }
        doc.pushes << push;
    }

    // The stack is rebuilt from the entries the uuids name, so a LoRA whose
    // entry has been deleted simply drops out.
    for (const QString& sha : state.activeLoraUuids) {
        for (const Entry& entry : m_entries->all()) {
            if (!entry.lora || entry.lora->sha256 != sha) continue;
            doc.loraStack << *entry.lora;
            break;
        }
    }

    m_deactivatedCategory = state.deactivatedCategory;

    // Bring back any rule the state knows about that the local rules.fct does
    // not, matched by uuid. An existing rule keeps its local behaviour unless
    // the setting says otherwise; its name is always local, since that is the
    // rule's display identity.
    int rulesAdded = 0;
    int rulesOverwritten = 0;
    {
        QHash<QString, qsizetype> byUuid;
        for (qsizetype i = 0; i < m_data->ruleFile.rules.size(); ++i)
            byUuid.insert(m_data->ruleFile.rules[i].uuid, i);

        for (const QJsonValue entry : state.rulesSnapshot) {
            const Rule restored = ruleFromJson(entry.toObject());
            if (restored.uuid.isEmpty()) continue;

            const auto it = byUuid.constFind(restored.uuid);
            if (it != byUuid.cend()) {
                if (!m_data->settings.forceOverwriteRulesOnStateLoad) continue;

                Rule& local = m_data->ruleFile.rules[it.value()];
                local.force = restored.force; // uuid and name stay local
                local.match = restored.match;
                local.action = restored.action;
                ++rulesOverwritten;
                continue;
            }

            byUuid.insert(restored.uuid, m_data->ruleFile.rules.size());
            m_data->ruleFile.rules.append(restored);
            ++rulesAdded;
        }
    }

    // The enabled flags and injected tags always come from the state. A rule
    // created after the snapshot is switched off, so what is on screen and
    // what is on disk agree.
    for (Rule& rule : m_data->ruleFile.rules) {
        const auto enabled = state.ruleStates.constFind(rule.uuid);
        if (enabled == state.ruleStates.cend()) {
            rule.enabled = false;
            continue;
        }
        rule.enabled = enabled.value();

        const auto arguments = state.ruleArguments.constFind(rule.uuid);
        if (arguments != state.ruleArguments.cend()) rule.action.arguments = arguments.value();
    }
    report(m_data->saveRules());
    rebuildRulesSidebar();

    // The state is canonical for variables: the set is replaced wholesale.
    QList<Variable> variables;
    for (const QPair<QString, QString>& pair : state.varValues)
        variables << Variable{pair.first, pair.second};
    m_data->varsFile.vars.setAll(variables);
    report(m_data->saveVariables());
    rebuildVarsSidebar();

    // The workflow selection is restored first, so the var restore below
    // lands on the right one.
    bool workflowMissing = false;
    if (!state.selectedWorkflowId.isEmpty()) {
        int found = -1;
        for (int i = 0; i < int(m_data->workflows.workflows.size()); ++i)
            if (m_data->workflows.workflows[i].id == state.selectedWorkflowId) found = i;

        if (found >= 0) {
            m_data->workflows.selectedIndex = found;
            rebuildWorkflowList();
        } else {
            workflowMissing = true;
        }
    }

    // Workflow vars are replaced wholesale in their saved order. Skipped when
    // the workflow is gone, so another workflow's vars are never clobbered.
    if (!workflowMissing && !state.selectedWorkflowId.isEmpty()) {
        QList<WorkflowVar> restored;
        for (const QJsonValue entry : state.workflowVarValues) {
            WorkflowVar var = varFromJson(entry.toObject());

            // An image the cache has lost is cleared: re-picking it beats
            // uploading a broken reference.
            if (auto* image = std::get_if<ImageVar>(&var.value)) {
                if (!image->imageUuid.isEmpty() && !m_cache->has(image->imageUuid)) {
                    emit statusMessage(u"Image input %1 missing from cache (%2) - repick"_s.arg(
                        var.placeholder, image->imageUuid.left(8)));
                    image->imageUuid.clear();
                }
            }
            restored << var;
        }
        m_data->workflows.workflows[m_data->workflows.selectedIndex].vars = std::move(restored);
    }

    report(m_data->saveWorkflows());
    emit workflowVarsChanged();

    // No redraw here: the store's reset below does it with the right order
    // already applied.
    applyProfileStamp(state, false);

    m_store->reset(std::move(doc));
    emit pushesChanged();

    QStringList warnings;
    if (workflowMissing) warnings << u"workflow no longer exists"_s;
    if (missingEntries > 0)
        warnings << u"%1 entr%2 no longer exist"_s.arg(missingEntries)
                        .arg(missingEntries == 1 ? u"y"_s : u"ies"_s);
    if (rulesAdded > 0)
        warnings << u"appended %1 rule%2 from state"_s.arg(rulesAdded)
                        .arg(rulesAdded == 1 ? QString() : u"s"_s);
    if (rulesOverwritten > 0)
        warnings << u"overwrote %1 rule%2 from state"_s.arg(rulesOverwritten)
                        .arg(rulesOverwritten == 1 ? QString() : u"s"_s);

    if (warnings.isEmpty())
        emit statusMessage(u"Restored: %1"_s.arg(state.name));
    else
        emit statusMessage(u"Restored: %1  (%2)"_s.arg(state.name, warnings.join(u", "_s)));
}

// ---- Session

void ComposerPage::saveSession(const QString& path) const
{
    const ComposerDoc& doc = m_store->doc();

    QJsonArray tags;
    for (const QString& tag : doc.activeTags)
        tags.append(tag);

    QJsonObject weights;
    for (auto it = doc.weights.cbegin(); it != doc.weights.cend(); ++it)
        if (qAbs(it.value() - 1.0f) >= 0.001f) weights[it.key()] = double(it.value());

    QJsonArray pushes;
    for (const EntryPush& push : doc.pushes)
        pushes.append(entryPushToJson(push));

    QJsonObject customFacets;
    for (auto it = doc.customFacets.cbegin(); it != doc.customFacets.cend(); ++it) {
        QJsonArray facets;
        for (const QString& facet : it.value())
            facets.append(facet);
        customFacets[it.key()] = facets;
    }

    QJsonArray deactivated;
    for (const QString& tag : doc.deactivated)
        deactivated.append(tag);

    QJsonObject deactivatedCategory;
    for (auto it = m_deactivatedCategory.cbegin(); it != m_deactivatedCategory.cend(); ++it)
        deactivatedCategory[it.key()] = it.value();

    QJsonArray loraUuids;
    for (const Lora& lora : doc.loraStack)
        loraUuids.append(lora.sha256);

    QJsonObject root;
    root[u"activeTags"_s] = tags;
    root[u"tagWeights"_s] = weights;
    root[u"activePushes"_s] = pushes;
    root[u"customTagFacets"_s] = customFacets;
    root[u"deactivatedTags"_s] = deactivated;
    root[u"deactivatedCategory"_s] = deactivatedCategory;
    root[u"activeLoraUuids"_s] = loraUuids;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument(root).toJson());
}

void ComposerPage::restoreSession(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();

    ComposerDoc doc;
    QSet<QString> seen;
    for (const QJsonValue value : root[u"activeTags"_s].toArray()) {
        const QString tag = value.toString();
        if (tag.isEmpty() || seen.contains(tag)) continue;
        seen.insert(tag);
        doc.activeTags << tag;
    }

    const QJsonObject weights = root[u"tagWeights"_s].toObject();
    for (auto it = weights.constBegin(); it != weights.constEnd(); ++it)
        doc.weights[it.key()] = float(it.value().toDouble(1.0));

    const QJsonObject customFacets = root[u"customTagFacets"_s].toObject();
    for (auto it = customFacets.constBegin(); it != customFacets.constEnd(); ++it) {
        QStringList facets;
        for (const QJsonValue facet : it.value().toArray())
            facets << facet.toString();
        if (!facets.isEmpty()) doc.customFacets[it.key()] = facets;
    }

    for (const QJsonValue value : root[u"deactivatedTags"_s].toArray()) {
        const QString tag = value.toString();
        if (!tag.isEmpty()) doc.deactivated.insert(tag);
    }

    m_deactivatedCategory.clear();
    const QJsonObject categories = root[u"deactivatedCategory"_s].toObject();
    for (auto it = categories.constBegin(); it != categories.constEnd(); ++it)
        m_deactivatedCategory[it.key()] = it.value().toString();

    // The same rule as a state restore: a push whose entry or image is gone
    // is dropped rather than kept claiming tags.
    for (const QJsonValue value : root[u"activePushes"_s].toArray()) {
        const EntryPush push = entryPushFromJson(value.toObject());
        const Entry* entry = m_entries->find(push.entryUuid);
        if (!entry) continue;

        for (const EntryImage& image : entry->images) {
            if (image.fileName != push.imageFile) continue;
            doc.pushes << push;
            break;
        }
    }

    for (const QJsonValue value : root[u"activeLoraUuids"_s].toArray()) {
        const QString sha = value.toString();
        for (const Entry& entry : m_entries->all()) {
            if (!entry.lora || entry.lora->sha256 != sha) continue;
            doc.loraStack << *entry.lora;
            break;
        }
    }

    m_store->reset(std::move(doc));
    emit pushesChanged();
}

// ---- PNG drops carrying a baked state
//
// An image this app queued carries the composer snapshot in a
// "tagcomposer_state" text chunk, so dropping one back on the page restores
// that state - the same gesture ComfyUI uses for a workflow image.

void ComposerPage::dragEnterEvent(QDragEnterEvent* event)
{
    if (bakedPngFromMime(event->mimeData()).isEmpty()) {
        QWidget::dragEnterEvent(event);
        return;
    }
    event->acceptProposedAction();
}

void ComposerPage::dropEvent(QDropEvent* event)
{
    const QString path = bakedPngFromMime(event->mimeData());
    if (path.isEmpty()) {
        QWidget::dropEvent(event);
        return;
    }
    event->acceptProposedAction();

    const QString json = readPngTextChunk(path, u"tagcomposer_state"_s);
    if (json.isEmpty()) {
        emit statusMessage(
            u"No baked composer state in %1"_s.arg(QFileInfo(path).fileName()));
        return;
    }

    const QJsonObject object = QJsonDocument::fromJson(json.toUtf8()).object();
    if (object.isEmpty()) {
        emit statusMessage(u"Baked composer state is unreadable - ignored"_s);
        return;
    }

    SavedState state = SavedState::fromJson(object);
    if (state.name.isEmpty()) state.name = QFileInfo(path).completeBaseName();
    restoreState(state);

    // Pinning the dropped image as the preview makes the load visible.
    const QImage image(path);
    if (!image.isNull()) setPreviewImage(image);
}

} // namespace tc
