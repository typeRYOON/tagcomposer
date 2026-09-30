#include <core/prompt.h>
#include <QHash>
#include <cmath>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

QString serializeForPrompt(QString tag)
{
    tag.replace(u"("_s, u"\\("_s);
    tag.replace(u")"_s, u"\\)"_s);
    tag.replace(u'_', u' ');
    return tag.toLower();
}

QString applyFormats(const PipelineTag& pt, const QList<FacetFormat>& formats)
{
    QString out = pt.tag;
    for (const FacetFormat& f : formats)
        if (pt.facets.contains(f.facet)) out = f.prefix + out + f.suffix;
    return out;
}

QString formatWeight(float w)
{
    QString s = QString::number(double(w), 'f', 2);
    while (s.endsWith(u'0'))
        s.chop(1);
    if (s.endsWith(u'.')) s.chop(1);
    return s;
}

} // namespace

QList<TagBucket> bucketByGroup(const QList<PipelineTag>& tags, const TagGroups& groups)
{
    QHash<QString, QList<PipelineTag>> buckets;
    for (const PipelineTag& pt : tags)
        buckets[groups.groupFor(pt.facets)] << pt;

    QList<TagBucket> ordered;
    for (const TagGroup& g : groups.all()) {
        const auto it = buckets.constFind(g.name);
        if (it == buckets.constEnd() || it->isEmpty()) continue;
        ordered << TagBucket{g.name, *it};
    }

    const auto uncategorized = buckets.constFind(QString());
    if (uncategorized != buckets.constEnd() && !uncategorized->isEmpty())
        ordered << TagBucket{QString(), *uncategorized};

    return ordered;
}

QString buildPromptString(const QList<TagBucket>& buckets, const QList<FacetFormat>& formats)
{
    QStringList parts;

    for (const TagBucket& b : buckets) {
        for (const PipelineTag& pt : b.tags) {
            if (!reachesOutput(pt.result)) continue;

            const QString text = serializeForPrompt(applyFormats(pt, formats));
            if (std::abs(pt.weight - 1.0f) < 0.0001f)
                parts << text;
            else
                parts << u"("_s + text + u":"_s + formatWeight(pt.weight) + u")"_s;
        }
    }

    return parts.join(u", "_s);
}

} // namespace tc
