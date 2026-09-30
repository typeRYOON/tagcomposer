#include <core/fct.h>
#include <QFile>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace tc {

QStringList splitList(QStringView text, QChar sep)
{
    QStringList out;
    for (QStringView part : text.split(sep, Qt::SkipEmptyParts)) {
        const QStringView t = part.trimmed();
        if (!t.isEmpty()) out << t.toString();
    }
    return out;
}

std::expected<FctDoc, LoadError> readFct(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::unexpected(LoadError{path, "cannot open: " + f.errorString()});

    const QString text = QString::fromUtf8(f.readAll());

    FctDoc doc;
    doc.eol = text.contains("\r\n"_L1) ? u"\r\n"_s : u"\n"_s;
    doc.trailingNewline = text.endsWith(u'\n');

    QStringList raws = text.split(u'\n');
    if (doc.trailingNewline && !raws.isEmpty()) raws.removeLast();

    doc.blocks << FctBlock{};

    for (QString raw : raws) {
        if (raw.endsWith(u'\r')) raw.chop(1);
        const QStringView trimmed = QStringView(raw).trimmed();

        if (trimmed.isEmpty() || trimmed.startsWith(u'#')) {
            doc.blocks.last().lines << FctLine{{}, {}, raw, true};
            continue;
        }

        if (trimmed.startsWith(u'@')) {
            FctBlock b;
            b.raw = raw;
            const qsizetype sp = trimmed.indexOf(u' ');
            if (sp < 0) {
                b.kind = trimmed.sliced(1).toString();
            }
            else {
                b.kind = trimmed.sliced(1, sp - 1).toString();
                b.name = trimmed.sliced(sp + 1).trimmed().toString();
            }
            doc.blocks << b;
            continue;
        }

        FctLine line;
        line.raw = raw;
        const qsizetype eq = trimmed.indexOf(u'=');
        if (eq >= 0) {
            line.key = trimmed.first(eq).trimmed().toString();
            line.values << trimmed.sliced(eq + 1).trimmed().toString();
        }
        else {
            line.values = splitList(trimmed);
        }
        doc.blocks.last().lines << line;
    }

    return doc;
}

std::expected<void, LoadError> writeFct(const FctDoc& doc, const QString& path)
{
    QString out;

    const auto put = [&out, &doc](const QString& s) {
        out += s;
        out += doc.eol;
    };

    for (const FctBlock& b : doc.blocks) {
        if (!b.kind.isEmpty()) {
            if (!b.raw.isEmpty()) {
                put(b.raw);
            }
            else {
                QString header = u"@"_s + b.kind;
                if (!b.name.isEmpty()) header += u' ' + b.name;
                put(header);
            }
        }

        for (const FctLine& l : b.lines) {
            if (l.trivia || !l.raw.isEmpty())
                put(l.raw);
            else if (!l.key.isEmpty())
                put(u"    "_s + l.key + u" = "_s + l.values.value(0));
            else
                put(u"    "_s + l.values.join(u", "_s));
        }
    }

    if (!doc.trailingNewline && out.endsWith(doc.eol)) out.chop(doc.eol.size());

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return std::unexpected(LoadError{path, "cannot open for writing: " + f.errorString()});

    f.write(out.toUtf8());
    if (!f.commit()) return std::unexpected(LoadError{path, "write failed: " + f.errorString()});

    return {};
}

} // namespace tc
