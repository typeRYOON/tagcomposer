// The tag list: filtering, group sections, and the per-tag row.

#include <app/composer_page.h>
#include <app/app_data.h>
#include <app/category_nav_panel.h>
#include <app/composer_scroll_area.h>
#include <app/icons.h>
#include <app/tag_preview_popup.h>
#include <app/tag_search_bar.h>
#include <QAbstractSpinBox>
#include <QCursor>
#include <QDoubleSpinBox>
#include <QGraphicsOpacityEffect>
#include <QInputDialog>
#include <QFontMetrics>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

QString dotColorFor(TagResult result)
{
    switch (result) {
    case TagResult::Include:
        return u"#336633"_s;
    case TagResult::Injected:
        return u"#44bb44"_s;
    case TagResult::Skipped:
        return u"#2a2a2a"_s;
    case TagResult::Replaced:
        return u"#552222"_s;
    case TagResult::Flagged:
        return u"#886622"_s;
    case TagResult::NoFacets:
        return u"#334466"_s;
    case TagResult::Deactivated:
        return u"#2a2a2a"_s;
    case TagResult::Deleted:
        return u"#2a2a2a"_s; // unreachable, but the switch stays exhaustive
    }
    return u"#444444"_s;
}

// A tag the rules never got to see, or one a rule injected that has no
// definition of its own.
bool isUndefined(const PipelineTag& tag)
{
    return tag.result == TagResult::NoFacets
        || (tag.result == TagResult::Injected && tag.facets.isEmpty());
}

const QRegularExpression& variableRe()
{
    static const QRegularExpression re(uR"(\$([A-Za-z0-9_]+)\$)"_s);
    return re;
}

QLabel* rowBadge(const QString& text, const QString& objectName)
{
    auto* badge = new QLabel(text);
    badge->setObjectName(objectName);
    badge->setAttribute(Qt::WA_StyledBackground, true);
    return badge;
}

} // namespace

void ComposerPage::applyTagFilter()
{
    // Every rebuild path lands here, so a pending search debounce is dropped;
    // otherwise typing then pressing Enter fires two fades back to back.
    if (m_filterDebounce) m_filterDebounce->stop();

    if (m_filterQuery.isEmpty() && !m_undefinedOnly && !m_pushedOnly) {
        rebuildGroupsDisplay(m_lastResult);
        return;
    }

    // The union of the pushes being shown, keyed the way weights are so a
    // $VAR$ row matches on the string that is actually in the document.
    QSet<QString> pushedKeys;
    if (m_pushedOnly) {
        for (const EntryPush& push : m_store->doc().pushes) {
            const QString key = push.entryUuid + u'\n' + push.imageFile;
            if (!m_pushedFilterKey.isEmpty() && key != m_pushedFilterKey) continue;
            for (const QString& tag : push.tags)
                pushedKeys.insert(tag);
        }
    }

    // The group a tag is shown under, which has to agree with the bucketing
    // in applyGroupsRebuild or filtering by a section name hides rows that
    // are sitting in it. A deactivated tag has no facets, so its section is
    // the one it was turned off in.
    auto displayGroupOf = [this](const PipelineTag& tag) {
        const QString group = tag.result == TagResult::Deactivated
            ? m_deactivatedCategory.value(documentKey(tag))
            : m_groups.groupFor(tag.facets);
        return group.isEmpty() ? u"Uncategorized"_s : group;
    };

    QList<PipelineTag> filtered;
    for (const PipelineTag& tag : m_lastResult) {
        if (m_undefinedOnly && !isUndefined(tag)) continue;
        if (m_pushedOnly && !pushedKeys.contains(documentKey(tag))) continue;

        if (!m_filterQuery.isEmpty()) {
            // A substring match, so "shirt" finds "black shirt" - the same
            // shape as the facet editor's filter.
            const bool matchesTag = tag.tag.contains(m_filterQuery, Qt::CaseInsensitive);
            const bool matchesSource = !tag.sourceTag.isEmpty()
                && tag.sourceTag.contains(m_filterQuery, Qt::CaseInsensitive);
            const bool matchesGroup =
                displayGroupOf(tag).contains(m_filterQuery, Qt::CaseInsensitive);
            if (!matchesTag && !matchesSource && !matchesGroup) continue;
        }
        filtered << tag;
    }
    rebuildGroupsDisplay(filtered);
}

void ComposerPage::rebuildGroupsDisplay(const QList<PipelineTag>& tags)
{
    // A rebuild landing while the states grid is up means the stack is about
    // to switch back, so the toggle and the floats are synced first rather
    // than left a step behind.
    leaveStatesViewMode();

    const bool fade = m_freezeNextRebuild;
    m_freezeNextRebuild = false;

    if (!fade) {
        applyGroupsRebuild(tags);
        return;
    }

    // Snap invisible, swap, fade back. The rebuild can change which child is
    // current, so the effect is resolved after the swap, not before.
    if (m_mainStackFade->state() == QAbstractAnimation::Running) m_mainStackFade->stop();
    applyGroupsRebuild(tags);

    QGraphicsOpacityEffect* effect = stackChildFx(m_mainStack->currentIndex());
    if (!effect) return;

    effect->setOpacity(0.0);
    m_mainStackFade->setTargetObject(effect);
    m_mainStackFade->setStartValue(0.0);
    m_mainStackFade->setEndValue(1.0);
    m_mainStackFade->start();
}

void ComposerPage::applyGroupsRebuild(const QList<PipelineTag>& tags)
{
    while (m_groupsLayout->count() > 0) {
        QLayoutItem* item = m_groupsLayout->takeAt(0);
        if (QWidget* widget = item->widget()) widget->deleteLater();
        delete item;
    }

    m_groupHeaders.clear();
    m_tagRowWidgets.clear();
    m_selectedRowIndex = -1;

    // Counted off the full result, not the filtered list, so the badge stays
    // right while a query has narrowed the view.
    int undefinedTotal = 0;
    for (const PipelineTag& tag : m_lastResult)
        if (isUndefined(tag)) ++undefinedTotal;

    // Defining the last undefined tag while filtered to them would strand the
    // user on an empty view, so the filter releases itself.
    if (m_undefinedToggleBtn && undefinedTotal == 0 && m_undefinedToggleBtn->isChecked()) {
        m_undefinedToggleBtn->setChecked(false); // re-enters through toggled
        return;
    }

    // Un-pushing the last entry would strand them the same way.
    if (m_pushedBtn) {
        const QList<EntryPush>& pushes = m_store->doc().pushes;
        m_pushedBtn->setEnabled(!pushes.isEmpty());

        bool keyStillThere = m_pushedFilterKey.isEmpty();
        for (const EntryPush& push : pushes)
            if (push.entryUuid + u'\n' + push.imageFile == m_pushedFilterKey)
                keyStillThere = true;

        if (m_pushedOnly && (pushes.isEmpty() || !keyStillThere)) {
            setPushedFilter(false, QString()); // re-enters through applyTagFilter
            return;
        }
    }

    if (m_undefinedToggleBtn) {
        m_undefinedToggleBtn->setText(undefinedTotal > 0 ? u"?  %1"_s.arg(undefinedTotal)
                                                         : u"?"_s);
        m_undefinedToggleBtn->setEnabled(undefinedTotal > 0 || m_undefinedOnly);
        m_undefinedToggleBtn->setProperty("warn", undefinedTotal > 0);
        m_undefinedToggleBtn->style()->unpolish(m_undefinedToggleBtn);
        m_undefinedToggleBtn->style()->polish(m_undefinedToggleBtn);
    }

    if (tags.isEmpty()) {
        m_mainStack->setCurrentIndex(0);
        if (m_categoryNav) m_categoryNav->updateCategories({});
        return;
    }
    m_mainStack->setCurrentIndex(1);

    QStringList navNames;
    auto addSection = [&](const QString& displayName, const QList<PipelineTag>& sectionTags) {
        auto* header = new QLabel(displayName);
        header->setObjectName(u"ComposerGroupHeader"_s);
        header->setContextMenuPolicy(Qt::CustomContextMenu);

        connect(header, &QWidget::customContextMenuRequested, this,
                [this, header, displayName](const QPoint& pos) {
                    QMenu menu;

                    // Uncategorized and Deactivated are synthetic: they have
                    // no facets to inherit, so only the full list applies.
                    bool real = false;
                    for (const TagGroup& group : m_groups.all())
                        if (group.name == displayName && !group.facets.isEmpty()) real = true;

                    if (real) {
                        menu.addAction(u"Add tag to %1..."_s.arg(displayName), this,
                                       [this, displayName]() { promptAddCustomTag(displayName); });
                        menu.addSeparator();
                    }
                    addCustomTagMenu(menu);
                    menu.exec(header->mapToGlobal(pos));
                });

        m_groupsLayout->addWidget(header);
        m_groupHeaders[displayName] = header;
        navNames << displayName;

        for (const PipelineTag& tag : sectionTags) {
            QWidget* row = makeTagRow(tag);
            m_groupsLayout->addWidget(row);
            m_tagRowWidgets << row;
        }

        auto* spacer = new QWidget;
        spacer->setFixedHeight(6);
        m_groupsLayout->addWidget(spacer);
    };

    // Display bucketing keeps a deactivated tag in the section it came from.
    // The output side re-buckets without them, so the prompt is unaffected.
    QHash<QString, QList<PipelineTag>> buckets;
    for (const PipelineTag& tag : tags) {
        const QString group = tag.result == TagResult::Deactivated
            ? m_deactivatedCategory.value(documentKey(tag))
            : m_groups.groupFor(tag.facets);
        buckets[group] << tag;
    }

    for (const TagGroup& group : m_groups.all()) {
        const auto it = buckets.constFind(group.name);
        if (it == buckets.cend() || it.value().isEmpty()) continue;
        addSection(group.name, it.value());
    }

    const auto uncategorized = buckets.constFind(QString());
    if (uncategorized != buckets.cend() && !uncategorized.value().isEmpty())
        addSection(u"Uncategorized"_s, uncategorized.value());

    m_groupsLayout->addStretch();

    // An undefined tag has no facets, so it always lands in Uncategorized.
    QHash<QString, int> undefinedByCategory;
    if (undefinedTotal > 0) undefinedByCategory[u"Uncategorized"_s] = undefinedTotal;
    if (m_categoryNav) m_categoryNav->updateCategories(navNames, undefinedByCategory);
}

void ComposerPage::setSelectedRow(int index)
{
    auto repolish = [](QWidget* widget) {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    };

    if (m_selectedRowIndex >= 0 && m_selectedRowIndex < m_tagRowWidgets.size()) {
        QWidget* previous = m_tagRowWidgets[m_selectedRowIndex];
        previous->setProperty("selected", false);
        repolish(previous);
    }

    m_selectedRowIndex = index;
    if (m_selectedRowIndex < 0 || m_selectedRowIndex >= m_tagRowWidgets.size()) return;

    QWidget* row = m_tagRowWidgets[m_selectedRowIndex];
    row->setProperty("selected", true);
    repolish(row);
    if (m_groupsScroll) m_groupsScroll->ensureWidgetVisible(row, 0, 24);
}

void ComposerPage::replaceTagVariable(const QString& oldKey, const QString& newVarName)
{
    if (!m_store->doc().activeTags.contains(oldKey)) return;

    QString newKey = oldKey;
    if (newVarName.isEmpty())
        newKey = stripVariables(newKey);
    else
        // Every $name$ is replaced, so the badge - which collapses them into
        // one pill - stays in step with the tag.
        newKey.replace(variableRe(), u"$"_s + newVarName + u"$"_s);

    newKey = newKey.trimmed();
    if (newKey.isEmpty() || newKey == oldKey) return;

    m_store->renameTag(oldKey, newKey);
}

QHash<QAction*, QString> ComposerPage::addQuickFacetActions(QMenu& menu) const
{
    const Settings& settings = m_data->settings;
    const QList<QPair<QString, QString>> entries{
        {u"character"_s, settings.quickCharacterFacet},
        {u"copyright"_s, settings.quickCopyrightFacet},
        {u"trigger word"_s, settings.quickTriggerWordFacet},
        {u"style"_s, settings.quickStyleFacet},
    };

    bool any = false;
    for (const auto& entry : entries)
        if (!entry.second.isEmpty()) any = true;
    if (!any) return {};

    menu.addSeparator();

    QHash<QAction*, QString> actions;
    for (const auto& entry : entries) {
        if (entry.second.isEmpty()) continue;
        QAction* action =
            menu.addAction(u"Quick add as %1 (%2)"_s.arg(entry.first, entry.second));
        actions.insert(action, entry.second);
    }
    return actions;
}

void ComposerPage::addCustomTagMenu(QMenu& menu)
{
    QMenu* submenu = menu.addMenu(u"Add tag to"_s);
    for (const TagGroup& group : m_groups.all()) {
        if (group.facets.isEmpty()) continue;
        const QString name = group.name;
        submenu->addAction(name, this, [this, name]() { promptAddCustomTag(name); });
    }
    if (submenu->isEmpty()) submenu->setEnabled(false);
}

void ComposerPage::promptAddCustomTag(const QString& groupName)
{
    const TagGroup* group = nullptr;
    for (const TagGroup& candidate : m_groups.all())
        if (candidate.name == groupName) group = &candidate;

    if (!group || group->facets.isEmpty()) {
        emit statusMessage(
            u"\"%1\" defines no facets - nothing to qualify for."_s.arg(groupName));
        return;
    }

    bool ok = false;
    const QString text = QInputDialog::getText(this, u"Add tag to %1"_s.arg(groupName),
                                               u"Text (natural language is fine):"_s,
                                               QLineEdit::Normal, QString(), &ok)
                             .trimmed();
    if (!ok || text.isEmpty()) return;

    if (m_activeTagSet.contains(text)) {
        // The same rule as the search bar: re-entering a manually deactivated
        // tag brings it back rather than reporting a no-op.
        if (m_store->doc().deactivated.contains(text)) {
            m_freezeNextRebuild = true;
            m_store->setDeactivated(text, false);
            emit statusMessage(u"Reactivated: %1"_s.arg(text));
        } else {
            emit statusMessage(u"Already in the composer: %1"_s.arg(text));
        }
        return;
    }

    m_freezeNextRebuild = true;
    m_store->addTags({text});
    m_store->setCustomFacets(text, group->facets);

    // groupFor is first match wins, so a broader group above this one claims
    // the facets before it gets here. Say so rather than silently misfiling.
    const QString lands = m_groups.groupFor(group->facets);
    if (lands != groupName)
        emit statusMessage(u"Added, but \"%1\" claims those facets before %2 does."_s.arg(
            lands.isEmpty() ? u"Uncategorized"_s : lands, groupName));
    else
        emit statusMessage(u"Added to %1: %2"_s.arg(groupName, text));
}

QWidget* ComposerPage::makeTagRow(const PipelineTag& tag)
{
    const bool hasVariable = !tag.sourceTag.isEmpty();
    const bool isDeactivated = tag.result == TagResult::Deactivated;
    const bool inDocument = tag.result != TagResult::Injected;
    const bool editable = !hasVariable && !isDeactivated
        && (tag.result == TagResult::Include || tag.result == TagResult::NoFacets
            || tag.result == TagResult::Flagged);

    const QString activeKey = documentKey(tag);

    auto* row = new QWidget;
    row->setObjectName(u"ComposerTagRow"_s);

    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(20, 1, 8, 1);
    layout->setSpacing(8);

    auto* dot = new QWidget;
    dot->setFixedSize(6, 6);
    dot->setStyleSheet(u"background:%1;border-radius:3px;"_s.arg(dotColorFor(tag.result)));
    layout->addWidget(dot, 0, Qt::AlignVCenter);

    auto* tagEdit = new QLineEdit(tag.tag);
    QString style = u"ComposerTagReadOnly"_s;
    if (editable)
        style = u"ComposerTagEdit"_s;
    else if (tag.result == TagResult::Injected)
        style = u"ComposerTagInjected"_s;
    else if (hasVariable)
        style = u"ComposerTagVar"_s;

    tagEdit->setObjectName(style);
    tagEdit->setReadOnly(!editable);
    tagEdit->setProperty("_tag", activeKey);

    if (tag.result == TagResult::Skipped || tag.result == TagResult::Replaced
        || tag.result == TagResult::Deactivated) {
        QFont font = tagEdit->font();
        font.setStrikeOut(true);
        tagEdit->setFont(font);
    }

    if (editable) {
        // A custom tag is free text rather than vocabulary, so it gets no
        // suggestions.
        if (!m_store->doc().customFacets.contains(activeKey))
            new TagLineAutocomplete(tagEdit, &m_data->danbooru, tagEdit);

        connect(tagEdit, &QLineEdit::editingFinished, this, [this, tagEdit]() {
            const QString from = tagEdit->property("_tag").toString();
            const QString to = tagEdit->text().trimmed();
            if (to.isEmpty() || to == from) return;
            if (!m_store->renameTag(from, to)) return;
            tagEdit->setProperty("_tag", to);
        });
    }

    layout->addWidget(tagEdit, 1);

    if (hasVariable) {
        QStringList names;
        auto it = variableRe().globalMatch(tag.sourceTag);
        while (it.hasNext())
            names << u"$"_s + it.next().captured(1) + u"$"_s;
        layout->addWidget(rowBadge(names.join(u' '), u"ComposerVarBadge"_s));
    }
    if (isUndefined(tag)) layout->addWidget(rowBadge(u"?"_s, u"ComposerNoBadge"_s));
    if (!tag.flagLabel.isEmpty())
        layout->addWidget(rowBadge(u"["_s + tag.flagLabel + u"]"_s, u"ComposerFlagBadge"_s));

    if (tag.result == TagResult::Injected) {
        auto* mark = new QLabel;
        mark->setObjectName(u"ComposerInjectedMark"_s);
        mark->setPixmap(icons::arrowUp(12, QColor(0x33, 0x66, 0x33)).pixmap(12, 12));
        layout->addWidget(mark);
        layout->addWidget(rowBadge(tag.ruleSource, u"ComposerInjectedBadge"_s));
    }

    // The weight only applies to a tag that reaches the output.
    if (tag.result != TagResult::Skipped && tag.result != TagResult::Replaced
        && tag.result != TagResult::Deactivated) {
        auto* weightSpin = new QDoubleSpinBox;
        weightSpin->setObjectName(u"ComposerWeightSpin"_s);
        weightSpin->setRange(0.10, 5.00);
        weightSpin->setSingleStep(0.05);
        weightSpin->setDecimals(2);
        weightSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        weightSpin->setAlignment(Qt::AlignCenter);
        weightSpin->setFixedWidth(40);
        weightSpin->setValue(double(tag.weight));

        auto applyWeightColor = [weightSpin](double value) {
            weightSpin->setProperty("weighted", qAbs(value - 1.0) > 0.001);
            weightSpin->style()->unpolish(weightSpin);
            weightSpin->style()->polish(weightSpin);
            weightSpin->update();
        };
        applyWeightColor(double(tag.weight));

        connect(weightSpin, &QDoubleSpinBox::valueChanged, this,
                [this, activeKey, applyWeightColor](double value) {
                    applyWeightColor(value);

                    // Tells refresh() the new value is already on screen, so
                    // it can skip the rebuild. Undo, a state restore or Clear
                    // weight all change weights too, and those do need the
                    // rows rebuilt for the spin boxes to show the change.
                    m_weightFromSpin = true;
                    m_store->setWeight(activeKey, float(value));
                    m_weightFromSpin = false;
                });
        layout->addWidget(weightSpin);
    }

    const QString wikiTag = tag.tag;

    if (!inDocument) {
        // An injected tag has no document entry, so it only gets the lookups.
        auto installWiki = [&](QWidget* target) {
            target->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(target, &QWidget::customContextMenuRequested, this,
                    [this, wikiTag](const QPoint&) {
                        QMenu menu;
                        QAction* wikiAction = menu.addAction(u"Wiki"_s);
                        installWikiPeek(menu, wikiAction, wikiTag, tagPreviewPopup());
                        QAction* facetAction = menu.addAction(u"Edit facets"_s);
                        const QHash<QAction*, QString> quickFacets = addQuickFacetActions(menu);

                        QAction* chosen = menu.exec(QCursor::pos());
                        tagPreviewPopup()->dismiss();

                        if (chosen == wikiAction)
                            emit wikiRequested(wikiTag);
                        else if (chosen == facetAction)
                            emit facetEditorRequested(wikiTag);
                        else if (chosen && quickFacets.contains(chosen))
                            emit quickFacetRequested(wikiTag, quickFacets.value(chosen));
                    });
        };
        installWiki(row);
        installWiki(tagEdit);
        return row;
    }

    // The row's category is captured now so a later rebuild can keep a
    // deactivated tag in the section it was turned off in.
    const QString originalCategory = m_groups.groupFor(tag.facets);
    auto onToggleDeactivate = [this, activeKey, originalCategory]() {
        const bool wasOff = m_store->doc().deactivated.contains(activeKey);
        if (wasOff)
            m_deactivatedCategory.remove(activeKey);
        else
            m_deactivatedCategory.insert(activeKey, originalCategory);
        m_store->setDeactivated(activeKey, !wasOff);
    };

    auto* removeBtn = new QPushButton(row);
    removeBtn->setObjectName(u"TagRemoveBtn"_s);
    removeBtn->setFixedSize(18, 18);
    removeBtn->setCursor(Qt::PointingHandCursor);
    icons::applyStates(removeBtn, icons::close, 9, QColor(0x3a, 0x3a, 0x3a),
                       QColor(0xcc, 0x33, 0x33));
    connect(removeBtn, &QPushButton::clicked, this, [this, activeKey]() {
        m_deactivatedCategory.remove(activeKey);
        m_store->removeTag(activeKey);
    });
    layout->addWidget(removeBtn, 0, Qt::AlignVCenter);

    auto installMenu = [&](QWidget* target) {
        target->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(target, &QWidget::customContextMenuRequested, this,
                [this, wikiTag, activeKey, hasVariable, isDeactivated,
                 onToggleDeactivate](const QPoint&) {
                    QMenu menu;
                    QAction* wikiAction = menu.addAction(u"Wiki"_s);
                    installWikiPeek(menu, wikiAction, wikiTag, tagPreviewPopup());

                    QAction* facetAction = menu.addAction(u"Edit facets"_s);
                    QAction* deactivateAction =
                        menu.addAction(isDeactivated ? u"Activate"_s : u"Deactivate"_s);
                    QAction* removeAction = menu.addAction(u"Remove"_s);

                    // The variable swap retargets every $foo$ or strips them.
                    QAction* dropVariableAction = nullptr;
                    QHash<QAction*, QString> setVariableActions;
                    if (hasVariable) {
                        QMenu* variableMenu = menu.addMenu(u"Change variable"_s);
                        dropVariableAction = variableMenu->addAction(u"Remove variable"_s);

                        const QList<Variable>& all = m_data->varsFile.vars.all();
                        if (!all.isEmpty()) variableMenu->addSeparator();
                        for (const Variable& variable : all) {
                            QAction* action =
                                variableMenu->addAction(u"$"_s + variable.name + u"$"_s);
                            setVariableActions.insert(action, variable.name);
                        }
                    }

                    const QHash<QAction*, QString> quickFacets = addQuickFacetActions(menu);

                    QAction* chosen = menu.exec(QCursor::pos());
                    tagPreviewPopup()->dismiss();

                    if (chosen == wikiAction)
                        emit wikiRequested(wikiTag);
                    else if (chosen == facetAction)
                        emit facetEditorRequested(wikiTag);
                    else if (chosen == deactivateAction)
                        onToggleDeactivate();
                    else if (chosen == removeAction)
                        m_store->removeTag(activeKey);
                    else if (chosen && quickFacets.contains(chosen))
                        emit quickFacetRequested(wikiTag, quickFacets.value(chosen));
                    else if (dropVariableAction && chosen == dropVariableAction)
                        replaceTagVariable(activeKey, QString());
                    else if (chosen && setVariableActions.contains(chosen))
                        replaceTagVariable(activeKey, setVariableActions.value(chosen));
                });
    };
    installMenu(row);
    installMenu(tagEdit);

    return row;
}

// ---- The pushed-entry filter

QString ComposerPage::pushLabel(const QString& key) const
{
    const qsizetype split = key.indexOf(u'\n');
    const QString uuid = split < 0 ? key : key.first(split);
    const QString imageFile = split < 0 ? QString() : key.sliced(split + 1);

    const Entry* entry = m_entries->find(uuid);
    QString title = entry ? entry->title : QString();
    if (title.isEmpty()) title = u"Entry %1"_s.arg(uuid.left(8));

    // Only worth disambiguating when it is not the entry's first image.
    if (entry && !entry->images.isEmpty() && entry->images[0].fileName != imageFile) {
        for (qsizetype i = 0; i < entry->images.size(); ++i)
            if (entry->images[i].fileName == imageFile)
                title += u" - image %1"_s.arg(i + 1);
    }
    return title;
}

void ComposerPage::setPushedFilter(bool on, const QString& key)
{
    m_pushedOnly = on;
    m_pushedFilterKey = on ? key : QString();
    updatePushedButton();
    m_freezeNextRebuild = true;
    applyTagFilter();
}

void ComposerPage::updatePushedButton()
{
    if (!m_pushedBtn) return;

    QString label = u"Pushed"_s;
    if (m_pushedOnly)
        label = m_pushedFilterKey.isEmpty() ? u"All pushed"_s : pushLabel(m_pushedFilterKey);

    // A title can be long and the button is a float sized to its text.
    const QFontMetrics metrics(m_pushedBtn->font());
    m_pushedBtn->setText(metrics.elidedText(label, Qt::ElideRight, 150));

    // The checked chrome inverts to light on blue, so the icon follows.
    m_pushedBtn->setIcon(icons::pushDown(
        11, m_pushedOnly ? QColor(0xff, 0xff, 0xff) : QColor(0x55, 0x55, 0x55)));
    m_pushedBtn->setProperty("on", m_pushedOnly);
    m_pushedBtn->style()->unpolish(m_pushedBtn);
    m_pushedBtn->style()->polish(m_pushedBtn);
    m_pushedBtn->adjustSize();

    repositionFloats(); // the width changed with the label
}

void ComposerPage::showPushedFilterMenu()
{
    if (!m_pushedBtn) return;

    struct Item {
        QString key;
        QString label;
        int count = 0;
    };

    QList<Item> items;
    for (const EntryPush& push : m_store->doc().pushes) {
        const QString key = push.entryUuid + u'\n' + push.imageFile;
        items << Item{key, pushLabel(key), int(push.tags.size())};
    }

    // Sorted so the menu does not shuffle between openings.
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        const int order = a.label.localeAwareCompare(b.label);
        return order != 0 ? order < 0 : a.key < b.key;
    });

    QMenu menu;
    if (items.isEmpty()) {
        menu.addAction(u"No entries pushed"_s)->setEnabled(false);
        menu.exec(m_pushedBtn->mapToGlobal(QPoint(0, m_pushedBtn->height())));
        return;
    }

    QAction* allAction = menu.addAction(u"All pushed (%1)"_s.arg(items.size()));
    allAction->setCheckable(true);
    allAction->setChecked(m_pushedOnly && m_pushedFilterKey.isEmpty());
    menu.addSeparator();

    for (const Item& item : items) {
        QAction* action = menu.addAction(u"%1  (%2)"_s.arg(item.label).arg(item.count));
        action->setCheckable(true);
        action->setChecked(m_pushedOnly && m_pushedFilterKey == item.key);
        action->setData(item.key);
    }

    menu.addSeparator();
    QAction* clearAction = menu.addAction(u"Show all tags"_s);
    clearAction->setEnabled(m_pushedOnly);

    QAction* chosen = menu.exec(m_pushedBtn->mapToGlobal(QPoint(0, m_pushedBtn->height())));
    if (!chosen) return;

    if (chosen == clearAction)
        setPushedFilter(false, QString());
    else if (chosen == allAction)
        setPushedFilter(true, QString());
    else
        setPushedFilter(true, chosen->data().toString());
}

void ComposerPage::togglePush(const QString& entryUuid, const QString& imageFile,
                              const QStringList& tags)
{
    if (m_store->isPushed(entryUuid, imageFile))
        m_store->unpush(entryUuid, imageFile);
    else
        m_store->push(EntryPush{entryUuid, imageFile, tags});

    emit pushesChanged();
}

} // namespace tc
