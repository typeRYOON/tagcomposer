#include <app/app_data.h>
#include <app/paths.h>
#include <core/facet_schema.h>
#include <core/rule_io.h>
#include <QElapsedTimer>
#include <QFileInfo>

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

void AppData::loadFailed(const char* relative, const LoadError& error)
{
    m_problems << QString::fromLatin1(relative).section(u'/', -1) + u": "_s + error.reason;
    trackLoad(relative, false);
}

void AppData::trackLoad(const char* relative, bool ok)
{
    const QString name = QString::fromLatin1(relative);
    if (!ok && QFileInfo::exists(dataPath(relative)))
        m_unwritable.insert(name);
    else
        m_unwritable.remove(name);
}

QString AppData::writeBlocked(const char* relative) const
{
    const QString name = QString::fromLatin1(relative);
    if (!m_unwritable.contains(name)) return {};
    return u"Not saved: %1 failed to load, so it is left untouched"_s.arg(
        name.section(u'/', -1));
}

QString AppData::saveRules()
{
    const QString blocked = writeBlocked(paths::kRules);
    if (!blocked.isEmpty()) return blocked;

    const auto written = writeRules(ruleFile, rulesPath());
    return written ? QString() : written.error().reason;
}

QString AppData::saveVariables()
{
    const QString blocked = writeBlocked(paths::kVars);
    if (!blocked.isEmpty()) return blocked;

    const auto written = writeVariables(varsFile, variablesPath());
    return written ? QString() : written.error().reason;
}

QString AppData::saveSettings()
{
    const QString blocked = writeBlocked(paths::kSettings);
    if (!blocked.isEmpty()) return blocked;

    const auto written = writeSettings(settings, dataPath(paths::kSettings));
    return written ? QString() : written.error().reason;
}

QString AppData::saveDefinitions()
{
    const QString blocked = writeBlocked(paths::kDefinitions);
    if (!blocked.isEmpty()) return blocked;

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
    trackLoad(paths::kRules, read.has_value());
    if (!read) return read.error().reason;
    ruleFile = std::move(*read);
    return {};
}

QString AppData::reloadVariables()
{
    auto read = readVariables(variablesPath());
    trackLoad(paths::kVars, read.has_value());
    if (!read) return read.error().reason;
    varsFile = std::move(*read);
    return {};
}

QString AppData::saveWorkflows()
{
    const QString blocked = writeBlocked(paths::kWorkflows);
    if (!blocked.isEmpty()) return blocked;

    const auto written = writeWorkflows(workflows, dataPath(paths::kWorkflows));
    return written ? QString() : written.error().reason;
}

QString AppData::workflowPath(const Workflow& workflow) const
{
    if (m_dataDir.isEmpty() || workflow.path.isEmpty()) return {};

    // Stored paths start with "data/"; don't double it.
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
    m_unwritable.clear();
    m_dataDir = dataDir;

    if (auto read = readSettings(dataPath(paths::kSettings)))
        settings = std::move(*read);
    else
        loadFailed(paths::kSettings, read.error());

    if (auto read = readFacetSchema(dataPath(paths::kFacets)))
        schema = std::move(*read);
    else
        loadFailed(paths::kFacets, read.error());

    if (auto read = readTagFacets(dataPath(paths::kDefinitions)))
        defsFile = std::move(*read);
    else
        loadFailed(paths::kDefinitions, read.error());

    if (auto read = readRules(dataPath(paths::kRules)))
        ruleFile = std::move(*read);
    else
        loadFailed(paths::kRules, read.error());

    if (auto read = readVariables(dataPath(paths::kVars)))
        varsFile = std::move(*read);
    else
        loadFailed(paths::kVars, read.error());

    if (auto read = readTagGroups(dataPath(paths::kGroups)))
        groups = std::move(read->groups);
    else
        loadFailed(paths::kGroups, read.error());

    if (auto read = readWorkflows(dataPath(paths::kWorkflows)))
        workflows = std::move(*read);
    else
        loadFailed(paths::kWorkflows, read.error());

    // Optional; feeds autocomplete and tag colors.
    danbooru.load(dataPath(paths::kDanbooruCsv));

    const QList<LoadError> entryErrors = entries.load(dataDir + u"/entry"_s);
    if (!entryErrors.isEmpty())
        m_problems << u"%1 entr%2 unreadable"_s.arg(entryErrors.size())
                          .arg(entryErrors.size() == 1 ? u"y"_s : u"ies"_s);

    emit loaded();
}

} // namespace tc
