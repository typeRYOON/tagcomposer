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

// Everything the app reads from disk, loaded once and owned by the shell.
//
// Pages do not take this: they take the two or three pieces they actually
// use, so a page's header still says what it depends on. This is the owner,
// not a handle to pass around.
class AppData : public QObject {
    Q_OBJECT

public:
    explicit AppData(QObject* parent = nullptr);

    // Reads system/ and entry/ under dataDir. Safe to call again; stores are
    // replaced in place, so references handed to pages stay valid.
    void load(const QString& dataDir);

    // Anything that could not be read. Empty on a clean load.
    const QStringList& problems() const;

    EntryStore entries;
    EntrySearch search{entries};
    ComposerStore composer;

    Settings settings;
    FacetSchema schema;
    TagGroups groups;
    WorkflowsFile workflows;
    DanbooruIndex danbooru;

    // Kept whole rather than unwrapped: a write has to put the file's own
    // header, line endings and trailing newline back.
    RuleFile ruleFile;
    VariablesFile varsFile;
    TagFacetsFile defsFile;

    // Write the edited halves back. Each reports what went wrong, if anything.
    QString saveRules();
    QString saveVariables();
    QString saveWorkflows();
    QString saveSettings();
    QString saveDefinitions();

    // Re-read one file after it was edited outside the app. Same contract:
    // empty on success. A failed reload leaves the loaded copy alone.
    QString reloadRules();
    QString reloadVariables();
    QString reloadSchema();

    QString rulesPath() const;
    QString variablesPath() const;

    // `relative` is one of the tc::paths constants.
    QString dataPath(const char* relative) const;
    const QString& dataDir() const;

    // Absolute path of a workflow's template, which Workflow::path stores
    // relative to the data dir.
    QString workflowPath(const Workflow& workflow) const;

signals:
    // The `.fct` values are plain data with no signals of their own, so this
    // is what tells a page to redraw after a load.
    void loaded();

private:
    QString m_dataDir;
    QStringList m_problems;
};

} // namespace tc
