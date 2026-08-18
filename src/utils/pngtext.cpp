#include <utils/pngtext.h>
#include <QFile>
#include <QtEndian>

namespace utils {

namespace {

// Raw zlib stream -> text. qUncompress wants a 4-byte big-endian size
// prefix; it grows the buffer itself when the hint is too low.
QString inflateZlib(const QByteArray& z, quint32 sizeHint)
{
    QByteArray buf(4, '\0');
    qToBigEndian<quint32>(qBound<quint32>(1024, sizeHint, 16u << 20), buf.data());
    buf.append(z);
    return QString::fromUtf8(qUncompress(buf));
}

} // namespace

QString readPngTextChunk(const QString& filePath, const QString& keyword)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return {};
    if (f.read(8) != QByteArray::fromHex("89504e470d0a1a0a")) return {};

    const QByteArray want = keyword.toLatin1();
    while (true) {
        const QByteArray head = f.read(8);
        if (head.size() < 8) return {};
        const quint32 len = qFromBigEndian<quint32>(head.constData());
        const QByteArray type = head.mid(4);
        if (type == "IEND") return {};

        const bool text = type == "tEXt" || type == "zTXt" || type == "iTXt";
        if (!text || len > (64u << 20)) {
            if (!f.seek(f.pos() + qint64(len) + 4)) return {}; // +4 = CRC
            continue;
        }

        const QByteArray data = f.read(len);
        if (quint32(data.size()) < len) return {};
        f.seek(f.pos() + 4);

        const int nul = int(data.indexOf('\0'));
        if (nul < 0 || data.left(nul) != want) continue;

        if (type == "tEXt") return QString::fromLatin1(data.mid(nul + 1));

        if (type == "zTXt") { // keyword \0 method(1) zlib
            if (nul + 2 >= data.size() || data[nul + 1] != 0) continue;
            return inflateZlib(data.mid(nul + 2), len * 8);
        }

        // iTXt: keyword \0 compressed(1) method(1) lang \0 translated \0 text
        const int flags = nul + 1;
        if (flags + 2 > data.size()) continue;
        const int langEnd = int(data.indexOf('\0', flags + 2));
        if (langEnd < 0) continue;
        const int transEnd = int(data.indexOf('\0', langEnd + 1));
        if (transEnd < 0) continue;
        const QByteArray body = data.mid(transEnd + 1);
        return data[flags] == 1 ? inflateZlib(body, len * 8) : QString::fromUtf8(body);
    }
}

} // namespace utils
