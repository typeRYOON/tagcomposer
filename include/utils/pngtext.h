#pragma once
#include <QString>

namespace utils {

// Value of the first tEXt/zTXt/iTXt chunk whose keyword matches, or empty.
// Raw chunk walk - no pixel decode, finds chunks anywhere in the file.
QString readPngTextChunk(const QString& filePath, const QString& keyword);

} // namespace utils
