#pragma once
#include <QString>

namespace tc {

// First tEXt/zTXt/iTXt chunk with this keyword, or empty. No pixel decode.
QString readPngTextChunk(const QString& filePath, const QString& keyword);

} // namespace tc
