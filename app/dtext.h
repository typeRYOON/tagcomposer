#pragma once
#include <QList>
#include <QString>

namespace tc {

// Danbooru DText to an HTML fragment for QTextBrowser. No <html> or <style>
// wrapper: the caller supplies its own, so the same markup renders at page
// scale or inside a side panel.
//
// outPostIds / outAssetIds collect the ids used by `!post #N` / `!asset #N`
// bullet galleries so the caller can fetch those thumbnails and register them
// as document resources named `post:<id>` / `asset:<id>`. Pass nullptr for
// both when the caller cannot: the bullets then render as plain links rather
// than broken images. thumbWidth / thumbHeight size the gallery cells.
QString dtextToHtml(const QString& dtext, QList<int>* outPostIds = nullptr,
                    QList<int>* outAssetIds = nullptr, int thumbWidth = 150,
                    int thumbHeight = 150);

} // namespace tc
