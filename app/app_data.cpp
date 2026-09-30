#include <app/app_data.h>
#include <app/paths.h>
#include <core/facet_schema.h>
#include <core/rule_io.h>
#include <QDir>
#include <QElapsedTimer>

using namespace Qt::StringLiterals;

namespace tc {

AppData::AppData(QObject* parent) : QObject(parent) {}

QString AppData::dataPath(const char* relative) const
{
    return m_dataDir + u"/"_s + QString::fromLatin1(relative);
}

const QString& AppData::dataDir() const
{
    return m_dataDir;
}

QString AppData::rulesPath() const
{
    return dataPath(paths::kRules);
}

QString AppData::variablesPath() const
{
    return dataPath(paths::kVars);
}

QString AppData::saveRules()
{
    const auto written = writeRules(ruleFile, rulesPath());
    return written ? QString() : written.error().reason;
}

QString AppData::saveVariables()
{
    const auto written = writeVariables(varsFile, variablesPath());
    return written ? QString() : written.error().reason;
}

QString AppData::saveSettings()
{
    const auto written = writeSettings(settings, dataPath(paths::kSettings));
    return written ? QString() : written.error().reason;
}

QString AppData::saveDefinitions()
{
    const auto written = writeTagFacets(defsFile, dataPath(paths::kDefinitions));
    return written ? QString() : written.error().reason;
}

QString AppData::reloadSchema()
{
    auto read = readFacetSchema(dataPath(paths::kFacets));
    if (!read) return read.error().reason;
    schema = std::move(*read);
    return {};
}

QString AppData::reloadRules()
{
    auto read = readRules(rulesPath());
    if (!read) return read.error().reason;
    ruleFile = std::move(*read);
    return {};
}

QString AppData::reloadVariables()
{
    auto read = readVariables(variablesPath());
    if (!read) return read.error().reason;
    varsFile = std::move(*read);
    return {};
}

QString AppData::saveWorkflows()
{
    const auto written = writeWorkflows(workflows, m_dataDir + u"/system/workflows.json"_s);
    return written ? QString() : written.error().reason;
}

QString AppData::workflowPath(const Workflow& workflow) const
{
    if (m_dataDir.isEmpty() || workflow.path.isEmpty()) return {};

    // Stored paths already start with "data/", so strip the data dir's own
    // trailing segment rather than doubling it.
    const QString relative = workflow.path.startsWith(u"data/"_s)
        ? workflow.path.sliced(5)
        : workflow.path;
    return m_dataDir + u"/"_s + relative;
}

const QStringList& AppData::problems() const
{
    return m_problems;
}

void AppData::load(const QString& dataDir)
{
    m_problems.clear();
    m_dataDir = dataDir;

    const QDir system(dataDir + u"/system"_s);

    if (auto read = readSettings(system.filePath(u"settings.json"_s)))
        settings = std::move(*read);
    else
        m_problems << u"settings.json: "_s + read.error().reason;

    if (auto read = readFacetSchema(system.filePath(u"facets.fct"_s)))
        schema = std::move(*read);
    else
        m_problems << u"facets.fct: "_s + read.error().reason;

    if (auto read = readTagFacets(system.filePath(u"tag_definitions.fct"_s)))
        defsFile = std::move(*read);
    else
        m_problems << u"tag_definitions.fct: "_s + read.error().reason;

    if (auto read = readRules(system.filePath(u"rules.fct"_s)))
        ruleFile = std::move(*read);
    else
        m_problems << u"rules.fct: "_s + read.error().reason;

    if (auto read = readVariables(system.filePath(u"vars.fct"_s)))
        varsFile = std::move(*read);
    else
        m_problems << u"vars.fct: "_s + read.error().reason;

    if (auto read = readTagGroups(system.filePath(u"groups.fct"_s)))
        groups = std::move(read->groups);
    else
        m_problems << u"groups.fct: "_s + read.error().reason;

    if (auto read = readWorkflows(system.filePath(u"workflows.json"_s)))
        workflows = std::move(*read);
    else
        m_problems << u"workflows.json: "_s + read.error().reason;

    // Optional: only autocomplete and the facet editor's tag colours use it.
    danbooru.load(dataPath(paths::kDanbooruCsv));

    const QList<LoadError> entryErrors = entries.load(dataDir + u"/entry"_s);
    if (!entryErrors.isEmpty())
        m_problems << u"%1 entr%2 unreadable"_s.arg(entryErrors.size())
                          .arg(entryErrors.size() == 1 ? u"y"_s : u"ies"_s);

    emit loaded();
}

} // namespace tc
