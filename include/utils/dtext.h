#pragma once
#include <QList>
#include <QString>

namespace utils {

// Danbooru DText -> an HTML fragment for QTextBrowser. No <html>/<style>
// wrapper: callers supply their own, so the same markup renders at page scale
// or inside a side panel.
//
// outPostIds / outAssetIds collect the ids used by `!post #N` / `!asset #N`
// bullet galleries, so the caller can fetch those thumbnails and register them
// as document resources named `post:<id>` / `asset:<id>`. Pass nullptr for both
// when the caller cannot: the bullets then render as plain links instead of
// broken images. thumbW/thumbH size the gallery cells.
QString dtextToHtml(const QString& dtext, QList<int>* outPostIds = nullptr,
                    QList<int>* outAssetIds = nullptr, int thumbW = 150, int thumbH = 150);

} // namespace utils
