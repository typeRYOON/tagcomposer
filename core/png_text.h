#pragma once
#include <QString>

namespace tc {

// The value of the first tEXt, zTXt or iTXt chunk whose keyword matches, or
// empty. A raw chunk walk, with no pixel decode, so it finds a chunk anywhere
// in the file.
QString readPngTextChunk(const QString& filePath, const QString& keyword);

} // namespace tc
