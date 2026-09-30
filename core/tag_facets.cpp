#include <core/tag_facets.h>
#include <core/fct.h>
#include <QFile>
#include <QSaveFile>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace tc {

QStringList TagFacets::facetsFor(const QString& tag) const
{
    return m_map.value(tag);
}

bool TagFacets::isDefined(const QString& tag) const
{
    return m_map.contains(tag);
}

void TagFacets::set(const QString& tag, const QStringList& facets)
{
    if (tag.isEmpty()) return;
    if (facets.isEmpty())
        m_map.remove(tag);
    else
        m_map.insert(tag, facets);
}

QStringList TagFacets::definedTags() const
{
    QStringList tags = m_map.keys();
    std::sort(tags.begin(), tags.end());
    return tags;
}

QStringList TagFacets::undefined(const QStringList& tags) const
{
    QStringList out;
    for (const QString& t : tags)
        if (!m_map.contains(t)) out << t;
    return out;
}

qsizetype TagFacets::size() const
{
    return m_map.size();
}

std::expected<TagFacetsFile, LoadError> readTagFacets(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::unexpected(LoadError{path, "cannot open: " + f.errorString()});

    const QString body = QString::fromUtf8(f.readAll());

    TagFacetsFile out;
    out.eol = body.contains("\r\n"_L1) ? u"\r\n"_s : u"\n"_s;
    out.trailingNewline = body.endsWith(u'\n');

    QStringList lines = body.split(u'\n');
    if (body.endsWith(u'\n') && !lines.isEmpty()) lines.removeLast();

    bool inHeader = true;
    int lineNo = 0;

    const auto warn = [&out, &path, &lineNo](const QString& why, const QString& text) {
        out.warnings << LoadError{QString("%1:%2").arg(path).arg(lineNo), why + ": " + text};
    };

    for (QString line : lines) {
        ++lineNo;
        if (line.endsWith(u'\r')) line.chop(1);
        const QString text = line.trimmed();

        if (text.isEmpty() || text.startsWith(u'#')) {
            if (inHeader) out.header << line;
            continue;
        }
        inHeader = false;

        const qsizetype eq = text.lastIndexOf(u'=');
        if (eq < 0) {
            warn("no '='", text);
            continue;
        }

        const QString tag = text.first(eq).trimmed();
        if (tag.isEmpty()) {
            warn("empty tag", text);
            continue;
        }

        const QStringList facets = splitList(QStringView(text).sliced(eq + 1));
        if (facets.isEmpty()) {
            warn("no facets", text);
            continue;
        }
        if (out.defs.isDefined(tag)) warn("duplicate tag, later wins", text);

        out.defs.set(tag, facets);
    }
    return out;
}

std::expected<void, LoadError> writeTagFacets(const TagFacetsFile& file, const QString& path)
{
    QString out;

    for (const QString& line : file.header) {
        out += line;
        out += file.eol;
    }

    for (const QString& tag : file.defs.definedTags()) {
        out += tag;
        out += u'=';
        out += file.defs.facetsFor(tag).join(u',');
        out += file.eol;
    }

    if (!file.trailingNewline && out.endsWith(file.eol)) out.chop(file.eol.size());

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return std::unexpected(LoadError{path, "cannot open for writing: " + f.errorString()});

    f.write(out.toUtf8());
    if (!f.commit()) return std::unexpected(LoadError{path, "write failed: " + f.errorString()});

    return {};
}

QList<FacetIssue> unknownFacets(const TagFacets& defs, const FacetSchema& schema)
{
    QList<FacetIssue> out;
    for (const QString& tag : defs.definedTags())
        for (const QString& facet : defs.facetsFor(tag))
            if (!schema.hasFacet(facet)) out << FacetIssue{tag, facet};
    return out;
}

} // namespace tc
