#pragma once
#include <QList>
#include <QString>

namespace tc {

// Danbooru DText to an HTML fragment for QTextBrowser (no <html>/<style>
// wrapper). outPostIds/outAssetIds collect !post/!asset gallery ids so the
// caller can register "post:<id>"/"asset:<id>" image resources; pass nullptr
// to render those bullets as links instead.
QString dtextToHtml(const QString& dtext, QList<int>* outPostIds = nullptr,
                    QList<int>* outAssetIds = nullptr, int thumbWidth = 150,
                    int thumbHeight = 150);

} // namespace tc
