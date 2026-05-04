#include <core/clusterfilter.h>
#include <QFile>
#include <QSet>
#include <QTextStream>

namespace core {

// File format:
//   # blank lines and comments ignored
//   mode = blacklist | whitelist
//   facetA                  (rule: must have facetA)
//   facetA, facetB          (rule: must have both)
// Saving rewrites the whole file - comments don't survive a round-trip.

static QList<QString> splitTrimmed(const QString& s, QChar sep)
{
    QList<QString> out;
    for (const QString& p : s.split(sep))
        if (const QString t = p.trimmed(); !t.isEmpty()) out << t;
    return out;
}

ClusterFilter ClusterFilter::loadFromFile(const QString& path)
{
    ClusterFilter f;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return f;

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;

        if (line.startsWith("mode", Qt::CaseInsensitive)) {
            const int eq = line.indexOf('=');
            if (eq >= 0) {
                const QString val = line.mid(eq + 1).trimmed().toLower();
                f.mode = (val == "whitelist") ? Mode::Whitelist : Mode::Blacklist;
            }
            continue;
        }

        const QList<QString> facets = splitTrimmed(line, ',');
        if (!facets.isEmpty()) f.rules << facets;
    }
    return f;
}

void ClusterFilter::saveToFile(const QString& path) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;

    QTextStream out(&file);
    out << "mode = " << (mode == Mode::Whitelist ? "whitelist" : "blacklist") << "\n\n";

    for (const QList<QString>& rule : rules)
        out << rule.join(", ") << "\n";
}

bool ClusterFilter::matches(const QList<QString>& tagFacets) const
{
    if (rules.isEmpty() || tagFacets.isEmpty()) return false;

    const QSet<QString> have(tagFacets.cbegin(), tagFacets.cend());
    for (const QList<QString>& rule : rules) {
        bool allPresent = true;
        for (const QString& need : rule) {
            if (!have.contains(need)) {
                allPresent = false;
                break;
            }
        }
        if (allPresent) return true;
    }
    return false;
}

bool ClusterFilter::keep(const QList<QString>& tagFacets) const
{
    const bool m = matches(tagFacets);
    return (mode == Mode::Whitelist) ? m : !m;
}

} // namespace core
