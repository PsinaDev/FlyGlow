#include "MediaInternal.h"

#include <QCoreApplication>
#include <QFile>
#include <QPainter>
#include <QtEndian>
#include <algorithm>
#include <array>

namespace fg::media::detail {

namespace {

constexpr char kSignature[] = "\x89PNG\r\n\x1a\n";

quint32 crc32(const QByteArray& type, const QByteArray& data)
{
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> t{};
        for (quint32 n = 0; n < 256; ++n) {
            quint32 c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    quint32 c = 0xffffffffu;
    for (const QByteArray* part : {&type, &data})
        for (char ch : *part)
            c = table[(c ^ quint8(ch)) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

void appendChunk(QByteArray& png, const QByteArray& type, const QByteArray& data)
{
    char be[4];
    qToBigEndian(quint32(data.size()), be);
    png.append(be, 4);
    png.append(type);
    png.append(data);
    qToBigEndian(crc32(type, data), be);
    png.append(be, 4);
}

struct Chunk {
    QByteArray type;
    QByteArray data;
};

struct ApngFrame {
    int width = 0, height = 0, x = 0, y = 0;
    double seconds = 0.1;
    int dispose = 0;   // 0 none, 1 background, 2 previous
    int blend = 0;     // 0 source, 1 over
    QByteArray data;
};

quint32 be32(const QByteArray& d, int at)
{
    return qFromBigEndian<quint32>(d.constData() + at);
}

quint16 be16(const QByteArray& d, int at)
{
    return qFromBigEndian<quint16>(d.constData() + at);
}

} // namespace

bool decodeApng(const QString& path, const DecodeOptions& options, const ProgressFn& progress, DecodedClip& clip)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > (qint64(256) << 20))
        return false;
    const QByteArray bytes = file.readAll();
    if (!bytes.startsWith(QByteArray(kSignature, 8)))
        return false;

    std::vector<Chunk> chunks;
    for (qsizetype at = 8; at + 12 <= bytes.size();) {
        const quint32 length = be32(bytes, int(at));
        if (at + 12 + qsizetype(length) > bytes.size())
            break;
        chunks.push_back({bytes.mid(at + 4, 4), bytes.mid(at + 8, length)});
        at += 12 + qsizetype(length);
        if (chunks.back().type == "IEND")
            break;
    }

    QByteArray header;
    QByteArray shared;
    bool animated = false;
    bool seenData = false;
    std::vector<ApngFrame> frames;
    for (const Chunk& c : chunks) {
        if (c.type == "IHDR") {
            header = c.data;
        } else if (c.type == "acTL") {
            animated = !seenData;
        } else if (c.type == "fcTL" && c.data.size() >= 26) {
            ApngFrame f;
            f.width = int(be32(c.data, 4));
            f.height = int(be32(c.data, 8));
            f.x = int(be32(c.data, 12));
            f.y = int(be32(c.data, 16));
            const int num = be16(c.data, 20);
            const int den = be16(c.data, 22);
            f.seconds = double(num) / (den == 0 ? 100 : den);
            f.dispose = std::clamp(int(quint8(c.data[24])), 0, 2);
            f.blend = std::clamp(int(quint8(c.data[25])), 0, 1);
            frames.push_back(f);
        } else if (c.type == "IDAT") {
            seenData = true;
            // An IDAT that no fcTL precedes is a still fallback, not part of the animation.
            if (!frames.empty())
                frames.back().data += c.data;
        } else if (c.type == "fdAT" && c.data.size() > 4) {
            if (!frames.empty())
                frames.back().data += c.data.mid(4);
        } else if (!seenData && c.type != "IEND") {
            QByteArray block;
            appendChunk(block, c.type, c.data);
            shared += block;
        }
    }
    if (!animated || header.size() != 13 || frames.size() < 2)
        return false;

    const int canvasW = int(be32(header, 0)), canvasH = int(be32(header, 4));
    if (canvasW <= 0 || canvasH <= 0 || qint64(canvasW) * canvasH > (qint64(1) << 26))
        return false;
    clip.sourceSize = QSize(canvasW, canvasH);

    QImage canvas(canvasW, canvasH, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    FrameCollector collector(options, 0, 0);
    double t = 0;
    for (size_t i = 0; i < frames.size(); ++i) {
        ApngFrame& f = frames[i];
        QByteArray png(kSignature, 8);
        QByteArray ihdr = header;
        qToBigEndian(quint32(f.width), ihdr.data());
        qToBigEndian(quint32(f.height), ihdr.data() + 4);
        appendChunk(png, "IHDR", ihdr);
        png += shared;
        appendChunk(png, "IDAT", f.data);
        appendChunk(png, "IEND", {});
        const QImage part = QImage::fromData(png, "PNG").convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (part.isNull())
            break;

        const QRect area(f.x, f.y, f.width, f.height);
        // Per the APNG spec, "previous" on the first frame acts as "background".
        const int dispose = i == 0 && f.dispose == 2 ? 1 : f.dispose;
        const QImage saved = dispose == 2 ? canvas.copy(area) : QImage();
        {
            QPainter p(&canvas);
            p.setCompositionMode(f.blend == 0 ? QPainter::CompositionMode_Source
                                              : QPainter::CompositionMode_SourceOver);
            p.drawImage(area.topLeft(), part);
        }

        // Same rule as for GIF: browsers stretch near-zero delays to 100 ms.
        const double d = f.seconds <= 0.010 ? 0.1 : f.seconds;
        if (collector.offer(t, d))
            collector.keep(fitFrame(canvas, options.maxSide));
        t += d;

        if (dispose == 1) {
            QPainter p(&canvas);
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.fillRect(area, Qt::transparent);
        } else if (dispose == 2) {
            QPainter p(&canvas);
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.drawImage(area.topLeft(), saved);
        }

        if (progress && !progress(double(i + 1) / frames.size())) {
            clip.cancelled = true;
            return true;
        }
    }
    clip.sourceDuration = t;
    collector.finish(clip);
    if (clip.frames.empty())
        clip.error = QCoreApplication::translate("MediaDecoder", "There are no frames in the selected range.");
    return true;
}

} // namespace fg::media::detail
