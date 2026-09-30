#pragma once
#include <core/load_error.h>
#include <QList>
#include <QString>
#include <QStringList>
#include <expected>

namespace tc {

// One physical line. A comment or blank line is trivia: only raw is set.
// A "key = value" line puts the value in values[0], unsplit; anything else is
// a comma list. Wrapped lists stay one FctLine per source line, so a schema
// that wants the whole list concatenates across them.
//
// raw is the verbatim source. The writer emits it as-is, which is what keeps
// an untouched file byte-identical. Clear raw after editing key or values and
// the writer regenerates that line instead.
struct FctLine {
    QString key;
    QStringList values;
    QString raw;
    bool trivia = false;
};

// kind is empty for the implicit run of lines before the first @block.
// A block owns every line up to the next @block, trailing comments included.
struct FctBlock {
    QString kind;
    QString name;
    QList<FctLine> lines;
    QString raw;
};

struct FctDoc {
    QList<FctBlock> blocks;
    QString eol = QStringLiteral("\r\n");
    bool trailingNewline = true;
};

std::expected<FctDoc, LoadError> readFct(const QString& path);
std::expected<void, LoadError> writeFct(const FctDoc& doc, const QString& path);

// Comma-separated, trimmed, empties dropped.
QStringList splitList(QStringView text, QChar sep = u',');

} // namespace tc
