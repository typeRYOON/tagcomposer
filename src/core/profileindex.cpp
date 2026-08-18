#include <core/profileindex.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <utility>

namespace core {

namespace {

constexpr const char* kHeader =
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

// Index of the first '|' outside a quoted run, or -1.
int barOutsideQuotes(const QString& s)
{
    bool inQuote = false;
    for (int i = 0; i < s.size(); ++i) {
        if (s[i] == QLatin1Char('"'))
            inQuote = !inQuote;
        else if (s[i] == QLatin1Char('|') && !inQuote)
            return i;
    }
    return -1;
}

// Trims, then strips one layer of surrounding quotes so spaces can be kept.
QString unquote(const QString& raw)
{
    const QString t = raw.trimmed();
    if (t.size() >= 2 && t.startsWith(QLatin1Char('"')) && t.endsWith(QLatin1Char('"')))
        return t.mid(1, t.size() - 2);
    return t;
}

QString quoteIfNeeded(const QString& v)
{
    if (v == v.trimmed() && !v.contains(QLatin1Char('|')) && !v.contains(QLatin1Char('"')))
        return v;
    return QStringLiteral("\"") + v + QStringLiteral("\"");
}

// true for `active = <group> | <format>` and `@active <group> | <format>`.
bool parseActiveLine(const QString& line, QString* group, QString* format)
{
    QString value;
    if (line.startsWith(QLatin1String("@active"))) {
        value = line.mid(7);
    }
    else {
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq < 0) return false;
        if (line.left(eq).trimmed().compare(QLatin1String("active"), Qt::CaseInsensitive) != 0)
            return false;
        value = line.mid(eq + 1);
    }
    const int bar = value.indexOf(QLatin1Char('|'));
    *group = (bar < 0 ? value : value.left(bar)).trimmed();
    *format = bar < 0 ? QString() : value.mid(bar + 1).trimmed();
    return true;
}

bool parseFormatLine(const QString& line, utils::FacetFormat* out)
{
    const int eq = line.indexOf(QLatin1Char('='));
    if (eq < 0) return false;
    const QString facet = line.left(eq).trimmed();
    if (facet.isEmpty()) return false;

    const QString rest = line.mid(eq + 1);
    const int bar = barOutsideQuotes(rest);
    out->facet = facet;
    out->prefix = unquote(bar < 0 ? rest : rest.left(bar));
    out->suffix = bar < 0 ? QString() : unquote(rest.mid(bar + 1));
    return true;
}

} // namespace

// ---- Free helpers

TagGroupIndex applyGroupOrder(const TagGroupIndex& base, const QStringList& order)
{
    if (order.isEmpty()) return base;

    const QList<TagGroup>& src = base.groups();
    QList<bool> taken(src.size(), false);
    QList<TagGroup> out;
    out.reserve(src.size());

    for (const QString& name : order) {
        for (int i = 0; i < src.size(); ++i) {
            if (taken[i] || src[i].name != name) continue;
            out << src[i];
            taken[i] = true;
            break;
        }
    }
    for (int i = 0; i < src.size(); ++i)
        if (!taken[i]) out << src[i];

    TagGroupIndex idx = base;
    idx.setGroups(std::move(out));
    return idx;
}

QStringList groupNames(const TagGroupIndex& index)
{
    QStringList out;
    out.reserve(index.groups().size());
    for (const TagGroup& g : index.groups())
        out << g.name;
    return out;
}

QStringList shadowedGroups(const TagGroupIndex& ordered)
{
    QStringList out;
    const QList<TagGroup>& gs = ordered.groups();
    for (int i = 0; i < gs.size(); ++i) {
        if (gs[i].facets.isEmpty()) continue; // groupFor skips these
        for (int j = i + 1; j < gs.size(); ++j) {
            if (gs[j].facets.isEmpty()) continue;
            if (gs[i].facets.size() > gs[j].facets.size()) continue;
            bool subset = true;
            for (const QString& f : gs[i].facets)
                if (!gs[j].facets.contains(f)) {
                    subset = false;
                    break;
                }
            if (subset) out << QString("%1 shadows %2").arg(gs[i].name, gs[j].name);
        }
    }
    return out;
}

// ---- Load / save

ProfileIndex ProfileIndex::loadFromFile(const QString& path)
{
    ProfileIndex idx;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return idx;

    enum class Block { None, Group, Format };
    Block block = Block::None;

    for (const QString& raw : QString::fromUtf8(f.readAll()).split('\n')) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;

        if (line.startsWith(QLatin1String("@groupprofile"))) {
            const QString name = line.mid(13).trimmed();
            block = Block::None;
            if (name.isEmpty()) continue;
            idx.m_groupProfiles << GroupProfile{name, {}};
            block = Block::Group;
            continue;
        }
        if (line.startsWith(QLatin1String("@formatprofile"))) {
            const QString name = line.mid(14).trimmed();
            block = Block::None;
            if (name.isEmpty()) continue;
            idx.m_formatProfiles << FormatProfile{name, {}};
            block = Block::Format;
            continue;
        }
        if (parseActiveLine(line, &idx.m_activeGroup, &idx.m_activeFormat)) continue;

        if (block == Block::Group) {
            for (const QString& part : line.split(','))
                if (const QString t = part.trimmed(); !t.isEmpty())
                    idx.m_groupProfiles.last().order << t;
        }
        else if (block == Block::Format) {
            utils::FacetFormat ff;
            if (parseFormatLine(line, &ff)) idx.m_formatProfiles.last().formats << ff;
        }
    }

    return idx;
}

ProfileIndex ProfileIndex::withDefaults(const TagGroupIndex& groups,
                                        const QList<utils::FacetFormat>& formats)
{
    ProfileIndex idx;
    idx.m_groupProfiles << GroupProfile{QStringLiteral("Default"), groupNames(groups)};
    idx.m_formatProfiles << FormatProfile{QStringLiteral("Default"), formats};
    idx.m_activeGroup = QStringLiteral("Default");
    idx.m_activeFormat = QStringLiteral("Default");
    return idx;
}

void ProfileIndex::saveToFile(const QString& path) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    QTextStream ts(&f);

    ts << kHeader;

    for (const GroupProfile& gp : m_groupProfiles) {
        ts << "@groupprofile " << gp.name << "\n";
        // Wrapped so a 40-group order stays readable; the parser accumulates
        // every body line of the block.
        QString row;
        for (int i = 0; i < gp.order.size(); ++i) {
            const bool last = (i + 1 == gp.order.size());
            row += gp.order[i];
            if (!last) row += ", ";
            if (last || row.size() >= 64) {
                ts << "    " << row.trimmed() << "\n"; // trailing ", " -> ","
                row.clear();
            }
        }
        ts << "\n";
    }

    for (const FormatProfile& fp : m_formatProfiles) {
        ts << "@formatprofile " << fp.name << "\n";
        for (const utils::FacetFormat& ff : fp.formats) {
            // trimmed so an empty suffix ends the line at the bar.
            const QString row =
                QStringLiteral("%1 = %2 | %3")
                    .arg(ff.facet, quoteIfNeeded(ff.prefix), quoteIfNeeded(ff.suffix));
            ts << "    " << row.trimmed() << "\n";
        }
        ts << "\n";
    }

    ts << "active = " << m_activeGroup << " | " << m_activeFormat << "\n";
}

void ProfileIndex::saveActiveToFile(const QString& path) const
{
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        saveToFile(path);
        return;
    }
    QStringList lines = QString::fromUtf8(in.readAll()).split('\n');
    in.close();

    // Keep the file's line endings: a hand-edited profiles.fct may be CRLF.
    QString replacement = QStringLiteral("active = %1 | %2").arg(m_activeGroup, m_activeFormat);
    if (!lines.isEmpty() && lines.first().endsWith(QLatin1Char('\r')))
        replacement += QLatin1Char('\r');

    bool replaced = false;
    for (QString& l : lines) {
        QString g;
        QString fmt;
        const QString t = l.trimmed();
        if (t.startsWith('#') || !parseActiveLine(t, &g, &fmt)) continue;
        l = replacement;
        replaced = true;
        break;
    }
    if (!replaced) {
        while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
            lines.removeLast();
        lines << QString() << replacement << QString();
    }

    QFile out(path);
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) out.write(lines.join('\n').toUtf8());
}

// ---- Lookup

const GroupProfile* ProfileIndex::groupProfile(const QString& name) const
{
    if (name.isEmpty()) return nullptr;
    for (const GroupProfile& gp : m_groupProfiles)
        if (gp.name == name) return &gp;
    return nullptr;
}

const FormatProfile* ProfileIndex::formatProfile(const QString& name) const
{
    if (name.isEmpty()) return nullptr;
    for (const FormatProfile& fp : m_formatProfiles)
        if (fp.name == name) return &fp;
    return nullptr;
}

QStringList ProfileIndex::activeOrder() const
{
    const GroupProfile* gp = groupProfile(m_activeGroup);
    return gp ? gp->order : QStringList();
}

QList<utils::FacetFormat> ProfileIndex::activeFormats() const
{
    const FormatProfile* fp = formatProfile(m_activeFormat);
    return fp ? fp->formats : QList<utils::FacetFormat>();
}

} // namespace core
