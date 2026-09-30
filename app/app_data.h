#pragma once
#include <core/composer_store.h>
#include <core/entry_search.h>
#include <core/danbooru_index.h>
#include <core/entry_store.h>
#include <core/rule_io.h>
#include <core/settings.h>
#include <core/tag_facets.h>
#include <core/tag_groups.h>
#include <core/variables.h>
#include <core/workflow_io.h>
#include <QObject>
#include <QStringList>

namespace tc {

// Everything loaded from disk, owned by the shell. Pages take only the pieces
// they use.
class AppData : public QObject {
    Q_OBJECT

public:
    explicit AppData(QObject* parent = nullptr);

    // Reads system/ and entry/. Reloading replaces the stores in place, so
    // references held by pages stay valid.
    void load(const QString& dataDir);

    // Load failures; empty on a clean load.
    const QStringList& problems() const;

    EntryStore entries;
    EntrySearch search{entries};
    ComposerStore composer;

    Settings settings;
    FacetSchema schema;
    TagGroups groups;
    WorkflowsFile workflows;
    DanbooruIndex danbooru;

    // Whole files, so writes keep their header and line endings.
    RuleFile ruleFile;
    VariablesFile varsFile;
    TagFacetsFile defsFile;

    // Return an error message, or empty on success.
    QString saveRules();
    QString saveVariables();
    QString saveWorkflows();
    QString saveSettings();
    QString saveDefinitions();

    // Re-read after an outside edit. Empty on success; on failure the copy is kept.
    QString reloadRules();
    QString reloadVariables();
    QString reloadSchema();

    QString rulesPath() const;
    QString variablesPath() const;

    // `relative` is one of the tc::paths constants.
    QString dataPath(const char* relative) const;
    const QString& dataDir() const;

    QString workflowPath(const Workflow& workflow) const;

signals:
    void loaded();

private:
    QString m_dataDir;
    QStringList m_problems;
};

} // namespace tc
