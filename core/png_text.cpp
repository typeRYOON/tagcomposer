#include <core/png_text.h>
#include <QFile>
#include <QtEndian>

namespace tc {
namespace {

// qUncompress needs a 4-byte big-endian size hint; it grows past a low one.
QString inflateZlib(const QByteArray& compressed, quint32 sizeHint)
{
    QByteArray buffer(4, '\0');
    qToBigEndian<quint32>(qBound<quint32>(1024, sizeHint, 16u << 20), buffer.data());
    buffer.append(compressed);
    return QString::fromUtf8(qUncompress(buffer));
}

} // namespace

QString readPngTextChunk(const QString& filePath, const QString& keyword)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    if (file.read(8) != QByteArray::fromHex("89504e470d0a1a0a")) return {};

    const QByteArray wanted = keyword.toLatin1();

    while (true) {
        const QByteArray header = file.read(8);
        if (header.size() < 8) return {};

        const quint32 length = qFromBigEndian<quint32>(header.constData());
        const QByteArray type = header.mid(4);
        if (type == "IEND") return {};

        const bool isText = type == "tEXt" || type == "zTXt" || type == "iTXt";
        if (!isText || length > (64u << 20)) {
            if (!file.seek(file.pos() + qint64(length) + 4)) return {}; // +4 for the CRC
            continue;
        }

        const QByteArray data = file.read(length);
        if (quint32(data.size()) < length) return {};
        file.seek(file.pos() + 4);

        const qsizetype nul = data.indexOf('\0');
        if (nul < 0 || data.first(nul) != wanted) continue;

        if (type == "tEXt") return QString::fromLatin1(data.sliced(nul + 1));

        if (type == "zTXt") { // keyword \0 method(1) zlib
            if (nul + 2 >= data.size() || data[nul + 1] != 0) continue;
            return inflateZlib(data.sliced(nul + 2), length * 8);
        }

        // iTXt: keyword \0 compressed(1) method(1) lang \0 translated \0 text
        const qsizetype flags = nul + 1;
        if (flags + 2 > data.size()) continue;

        const qsizetype langEnd = data.indexOf('\0', flags + 2);
        if (langEnd < 0) continue;

        const qsizetype translatedEnd = data.indexOf('\0', langEnd + 1);
        if (translatedEnd < 0) continue;

        const QByteArray body = data.sliced(translatedEnd + 1);
        return data[flags] == 1 ? inflateZlib(body, length * 8) : QString::fromUtf8(body);
    }
}

} // namespace tc
