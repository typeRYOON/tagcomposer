#pragma once
#include <core/load_error.h>
#include <QList>
#include <QString>
#include <QStringList>
#include <expected>

namespace tc {

// One physical line. Comments and blanks are trivia (raw only). "key = value"
// keeps the value unsplit in values[0]; other lines are comma lists. The writer
// emits raw verbatim; clear it after an edit to regenerate the line.
struct FctLine {
    QString key;
    QStringList values;
    QString raw;
    bool trivia = false;
};

// kind is empty for lines before the first @block.
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
