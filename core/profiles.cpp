#include <core/profiles.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <utility>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

constexpr auto kHeader =
    "# TagComposer - Group and Format Profiles\n"
    "#\n"
    "# @groupprofile Name    a named ordering of the groups defined in groups.fct.\n"
    "#     A, B, C           Comma separated, may span several lines. Naming a\n"
    "#                       subset is fine - groups left out keep their\n"
    "#                       groups.fct order and follow the named ones.\n"
    "#\n"
    "# @formatprofile Name   a named set of per-facet tag wraps.\n"
    "#     facet = pre | suf Either side may be empty. Quote a side to keep\n"
    "#                       leading/trailing spaces: rStyle = \"@ \" | \"\"\n"
    "#\n"
    "# active = <groupprofile> | <formatprofile>\n"
    "#\n"
    "# Reordering changes which group *claims* a tag, not just where it lands in\n"
    "# the prompt: groups match top-to-bottom, first match wins. Putting a broad\n"
    "# group (rBody) above a narrow one (rBody, Hair) makes the narrow one dead.\n"
    "# 'active' is reserved and cannot be used as a facet name.\n\n";

// The first '|' outside a quoted run, or -1.
qsizetype barOutsideQuotes(const QString& text)
{
    bool inQuote = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text[i] == u'"')
            inQuote = !inQuote;
        else if (text[i] == u'|' && !inQuote)
            return i;
    }
    return -1;
}

// Trims, then strips one layer of quotes.
QString unquote(const QString& raw)
{
    const QString trimmed = raw.trimmed();
    if (trimmed.size() >= 2 && trimmed.startsWith(u'"') && trimmed.endsWith(u'"'))
        return trimmed.sliced(1, trimmed.size() - 2);
    return trimmed;
}

QString quoteIfNeeded(const QString& value)
{
    if (value == value.trimmed() && !value.contains(u'|') && !value.contains(u'"'))
        return value;
    return u"\""_s + value + u"\""_s;
}

// Accepts both `active = <group> | <format>` and `@active <group> | <format>`.
bool parseActiveLine(const QString& line, QString* group, QString* format)
{
    QString value;
    if (line.startsWith("@active"_L1)) {
        value = line.sliced(7);
    } else {
        const qsizetype equals = line.indexOf(u'=');
        if (equals < 0) return false;
        if (line.first(equals).trimmed().compare("active"_L1, Qt::CaseInsensitive) != 0)
            return false;
        value = line.sliced(equals + 1);
    }

    const qsizetype bar = value.indexOf(u'|');
    *group = (bar < 0 ? value : value.first(bar)).trimmed();
    *format = bar < 0 ? QString() : value.sliced(bar + 1).trimmed();
    return true;
}

bool parseFormatLine(const QString& line, FacetFormat* out)
{
    const qsizetype equals = line.indexOf(u'=');
    if (equals < 0) return false;

    const QString facet = line.first(equals).trimmed();
    if (facet.isEmpty()) return false;

    const QString rest = line.sliced(equals + 1);
    const qsizetype bar = barOutsideQuotes(rest);

    out->facet = facet;
    out->prefix = unquote(bar < 0 ? rest : rest.first(bar));
    out->suffix = bar < 0 ? QString() : unquote(rest.sliced(bar + 1));
    return true;
}

} // namespace

TagGroups applyGroupOrder(const TagGroups& base, const QStringList& order)
{
    if (order.isEmpty()) return base;

    const QList<TagGroup>& source = base.all();
    QList<bool> taken(source.size(), false);

    QList<TagGroup> ordered;
    ordered.reserve(source.size());

    for (const QString& name : order) {
        for (qsizetype i = 0; i < source.size(); ++i) {
            if (taken[i] || source[i].name != name) continue;
            ordered << source[i];
            taken[i] = true;
            break;
        }
    }
    for (qsizetype i = 0; i < source.size(); ++i)
        if (!taken[i]) ordered << source[i];

    TagGroups result;
    result.setAll(std::move(ordered));
    return result;
}

ProfileIndex ProfileIndex::loadFromFile(const QString& path)
{
    ProfileIndex index;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return index;

    enum class Block { None, Group, Format };
    Block block = Block::None;

    for (const QString& raw : QString::fromUtf8(file.readAll()).split(u'\n')) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(u'#')) continue;

        if (line.startsWith("@groupprofile"_L1)) {
            const QString name = line.sliced(13).trimmed();
            block = Block::None;
            if (name.isEmpty()) continue;
            index.m_groupProfiles << GroupProfile{name, {}};
            block = Block::Group;
            continue;
        }
        if (line.startsWith("@formatprofile"_L1)) {
            const QString name = line.sliced(14).trimmed();
            block = Block::None;
            if (name.isEmpty()) continue;
            index.m_formatProfiles << FormatProfile{name, {}};
            block = Block::Format;
            continue;
        }
        if (parseActiveLine(line, &index.m_activeGroup, &index.m_activeFormat)) continue;

        if (block == Block::Group) {
            for (const QString& part : line.split(u',')) {
                const QString name = part.trimmed();
                if (!name.isEmpty()) index.m_groupProfiles.last().order << name;
            }
        } else if (block == Block::Format) {
            FacetFormat format;
            if (parseFormatLine(line, &format)) index.m_formatProfiles.last().formats << format;
        }
    }

    return index;
}

ProfileIndex ProfileIndex::withDefaults(const TagGroups& groups,
                                        const QList<FacetFormat>& formats)
{
    ProfileIndex index;
    index.m_groupProfiles << GroupProfile{u"Default"_s, groups.names()};
    index.m_formatProfiles << FormatProfile{u"Default"_s, formats};
    index.m_activeGroup = u"Default"_s;
    index.m_activeFormat = u"Default"_s;
    return index;
}

void ProfileIndex::saveToFile(const QString& path) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;

    QTextStream out(&file);
    out << kHeader;

    for (const GroupProfile& profile : m_groupProfiles) {
        out << "@groupprofile " << profile.name << "\n";

        // Wrap long orders; the parser reads every body line.
        QString row;
        for (qsizetype i = 0; i < profile.order.size(); ++i) {
            const bool last = i + 1 == profile.order.size();
            row += profile.order[i];
            if (!last) row += u", "_s;
            if (!last && row.size() < 64) continue;
            out << "    " << row.trimmed() << "\n";
            row.clear();
        }
        out << "\n";
    }

    for (const FormatProfile& profile : m_formatProfiles) {
        out << "@formatprofile " << profile.name << "\n";
        for (const FacetFormat& format : profile.formats) {
            const QString row = u"%1 = %2 | %3"_s.arg(
                format.facet, quoteIfNeeded(format.prefix), quoteIfNeeded(format.suffix));
            out << "    " << row.trimmed() << "\n";
        }
        out << "\n";
    }

    out << "active = " << m_activeGroup << " | " << m_activeFormat << "\n";
}

void ProfileIndex::saveActiveToFile(const QString& path) const
{
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) {
        saveToFile(path);
        return;
    }
    QStringList lines = QString::fromUtf8(input.readAll()).split(u'\n');
    input.close();

    // Keep CRLF if the file uses it.
    QString replacement = u"active = %1 | %2"_s.arg(m_activeGroup, m_activeFormat);
    if (!lines.isEmpty() && lines.first().endsWith(u'\r')) replacement += u'\r';

    bool replaced = false;
    for (QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(u'#')) continue;

        QString group;
        QString format;
        if (!parseActiveLine(trimmed, &group, &format)) continue;

        line = replacement;
        replaced = true;
        break;
    }

    if (!replaced) {
        while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
            lines.removeLast();
        lines << QString() << replacement << QString();
    }

    QFile output(path);
    if (output.open(QIODevice::WriteOnly | QIODevice::Truncate))
        output.write(lines.join(u'\n').toUtf8());
}

bool ProfileIndex::isEmpty() const
{
    return m_groupProfiles.isEmpty() && m_formatProfiles.isEmpty();
}

const QList<GroupProfile>& ProfileIndex::groupProfiles() const
{
    return m_groupProfiles;
}

const QList<FormatProfile>& ProfileIndex::formatProfiles() const
{
    return m_formatProfiles;
}

const GroupProfile* ProfileIndex::groupProfile(const QString& name) const
{
    if (name.isEmpty()) return nullptr;
    for (const GroupProfile& profile : m_groupProfiles)
        if (profile.name == name) return &profile;
    return nullptr;
}

const FormatProfile* ProfileIndex::formatProfile(const QString& name) const
{
    if (name.isEmpty()) return nullptr;
    for (const FormatProfile& profile : m_formatProfiles)
        if (profile.name == name) return &profile;
    return nullptr;
}

const QString& ProfileIndex::activeGroup() const
{
    return m_activeGroup;
}

const QString& ProfileIndex::activeFormat() const
{
    return m_activeFormat;
}

void ProfileIndex::setActiveGroup(const QString& name)
{
    m_activeGroup = name;
}

void ProfileIndex::setActiveFormat(const QString& name)
{
    m_activeFormat = name;
}

QStringList ProfileIndex::activeOrder() const
{
    const GroupProfile* profile = groupProfile(m_activeGroup);
    return profile ? profile->order : QStringList();
}

QList<FacetFormat> ProfileIndex::activeFormats() const
{
    const FormatProfile* profile = formatProfile(m_activeFormat);
    return profile ? profile->formats : QList<FacetFormat>();
}

} // namespace tc
