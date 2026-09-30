#include <core/cluster_filter.h>
#include <QFile>
#include <QSet>
#include <QTextStream>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

// File format:
//   # comments and blank lines are skipped
//   mode = blacklist | whitelist
//   character              one rule, one facet
//   character, female      one rule, both required
//   -nsfw                  a negation, which always drops
//   character, -nsfw       a rule and a negation on one line
//
// A save rewrites the file whole, so comments do not survive a round trip.
// This is not readFct's format: those files carry @blocks and this one is a
// flat list the user edits in a text box.
QStringList splitTrimmed(const QString& text, QChar separator)
{
    QStringList parts;
    for (const QString& part : text.split(separator))
        if (const QString trimmed = part.trimmed(); !trimmed.isEmpty()) parts << trimmed;
    return parts;
}

} // namespace

ClusterFilter ClusterFilter::load(const QString& path)
{
    ClusterFilter filter;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return filter;

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(u'#')) continue;

        if (line.startsWith("mode"_L1, Qt::CaseInsensitive)) {
            const qsizetype equals = line.indexOf(u'=');
            if (equals >= 0) {
                const QString value = line.mid(equals + 1).trimmed().toLower();
                filter.mode = value == "whitelist"_L1 ? Mode::Whitelist : Mode::Blacklist;
            }
            continue;
        }

        QStringList positive;
        for (const QString& token : splitTrimmed(line, u',')) {
            if (!token.startsWith(u'-')) {
                positive << token;
                continue;
            }
            const QString name = token.sliced(1).trimmed();
            if (!name.isEmpty() && !filter.negations.contains(name)) filter.negations << name;
        }
        if (!positive.isEmpty()) filter.rules << positive;
    }
    return filter;
}

bool ClusterFilter::save(const QString& path) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;

    QTextStream out(&file);
    out << "mode = " << (mode == Mode::Whitelist ? "whitelist" : "blacklist") << "\n\n";

    if (!negations.isEmpty()) {
        QStringList prefixed;
        prefixed.reserve(negations.size());
        for (const QString& negation : negations) prefixed << u"-"_s + negation;
        out << prefixed.join(u", "_s) << "\n";
    }

    for (const QStringList& rule : rules) out << rule.join(u", "_s) << "\n";
    return true;
}

bool ClusterFilter::matches(const QStringList& tagFacets) const
{
    if (rules.isEmpty() || tagFacets.isEmpty()) return false;

    const QSet<QString> have(tagFacets.cbegin(), tagFacets.cend());
    for (const QStringList& rule : rules) {
        bool all = true;
        for (const QString& needed : rule) {
            if (have.contains(needed)) continue;
            all = false;
            break;
        }
        if (all) return true;
    }
    return false;
}

bool ClusterFilter::keep(const QStringList& tagFacets) const
{
    // Before the mode, not after: a negation means drop, whichever way round
    // the rules are being read.
    if (!negations.isEmpty() && !tagFacets.isEmpty()) {
        const QSet<QString> have(tagFacets.cbegin(), tagFacets.cend());
        for (const QString& negation : negations)
            if (have.contains(negation)) return false;
    }

    const bool matched = matches(tagFacets);
    return mode == Mode::Whitelist ? matched : !matched;
}

} // namespace tc
