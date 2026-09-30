#include <core/tag_groups.h>
#include <core/fct.h>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace tc {

QString TagGroups::groupFor(const QStringList& tagFacets) const
{
    for (const TagGroup& g : m_groups) {
        if (g.facets.isEmpty()) continue;

        bool all = true;
        for (const QString& f : g.facets) {
            if (!tagFacets.contains(f)) {
                all = false;
                break;
            }
        }
        if (all) return g.name;
    }
    return {};
}

const QList<TagGroup>& TagGroups::all() const
{
    return m_groups;
}

void TagGroups::setAll(QList<TagGroup> groups)
{
    m_groups = std::move(groups);
}

QStringList TagGroups::names() const
{
    QStringList out;
    out.reserve(m_groups.size());
    for (const TagGroup& g : m_groups)
        out << g.name;
    return out;
}

std::expected<TagGroupsFile, LoadError> readTagGroups(const QString& path)
{
    const std::expected<FctDoc, LoadError> doc = readFct(path);
    if (!doc) return std::unexpected(doc.error());

    TagGroupsFile out;
    out.eol = doc->eol;
    out.trailingNewline = doc->trailingNewline;

    QList<TagGroup> groups;

    for (const FctBlock& b : doc->blocks) {
        if (b.kind.isEmpty()) {
            for (const FctLine& l : b.lines) {
                if (!l.trivia) break;
                out.header << l.raw;
            }
            continue;
        }
        if (b.kind != "group"_L1) continue;

        if (b.name.isEmpty()) {
            out.warnings << LoadError{path, u"group with an empty name, skipped"_s};
            continue;
        }

        TagGroup g;
        g.name = b.name;
        for (const FctLine& l : b.lines) {
            if (l.trivia || !l.key.isEmpty()) continue;
            g.facets << l.values;
        }

        if (g.facets.isEmpty())
            out.warnings << LoadError{path, u"group \""_s + g.name + u"\" has no facets, it can never match"_s};

        groups << g;
    }

    out.groups.setAll(std::move(groups));
    return out;
}

std::expected<void, LoadError> writeTagGroups(const TagGroupsFile& file, const QString& path)
{
    QString out;

    for (const QString& h : file.header) {
        out += h;
        out += file.eol;
    }

    for (const TagGroup& g : file.groups.all()) {
        out += u"@group "_s + g.name + file.eol;
        out += u"    "_s + g.facets.join(u", "_s) + file.eol;
        out += file.eol;
    }

    if (!file.groups.all().isEmpty()) out.chop(file.eol.size());
    if (!file.trailingNewline && out.endsWith(file.eol)) out.chop(file.eol.size());

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return std::unexpected(LoadError{path, "cannot open for writing: " + f.errorString()});

    f.write(out.toUtf8());
    if (!f.commit()) return std::unexpected(LoadError{path, "write failed: " + f.errorString()});

    return {};
}

QList<GroupShadow> shadowedGroups(const TagGroups& groups)
{
    QList<GroupShadow> out;
    const QList<TagGroup>& gs = groups.all();

    for (qsizetype i = 0; i < gs.size(); ++i) {
        if (gs[i].facets.isEmpty()) continue;
        for (qsizetype j = i + 1; j < gs.size(); ++j) {
            if (gs[j].facets.isEmpty()) continue;
            if (gs[i].facets.size() > gs[j].facets.size()) continue;

            bool subset = true;
            for (const QString& f : gs[i].facets) {
                if (!gs[j].facets.contains(f)) {
                    subset = false;
                    break;
                }
            }
            if (subset) out << GroupShadow{gs[i].name, gs[j].name};
        }
    }
    return out;
}

} // namespace tc
