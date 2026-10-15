#include "FdgFraming.h"

#include <QtEndian>

#include <array>
#include <cstdint>
#include <cstring>

namespace fg::fdg {

namespace {

void appendLe32(QByteArray& out, qint32 v)
{
    char b[4];
    qToLittleEndian<qint32>(v, b);
    out.append(b, 4);
}

qint32 readLe32(const char* p)
{
    return qFromLittleEndian<qint32>(p);
}

QByteArray headerLine(const char* name)
{
    QByteArray h(name);
    h.append('\n');
    return h;
}

// RFC 1951 inflate modelled on zlib's contrib/puff, decoding canonical Huffman codes one bit at a time.
struct Huffman {
    short count[16];
    short symbol[288];
};

// Returns 0 for a complete code, > 0 for an incomplete one, < 0 for an over-subscribed one.
int construct(Huffman& h, const short* length, int n)
{
    for (short& c : h.count)
        c = 0;
    for (int sym = 0; sym < n; ++sym)
        h.count[length[sym]]++;
    if (h.count[0] == n)
        return 0;
    int left = 1;
    for (int len = 1; len < 16; ++len) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0)
            return left;
    }
    short offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; ++len)
        offs[len + 1] = short(offs[len] + h.count[len]);
    for (int sym = 0; sym < n; ++sym)
        if (length[sym] != 0)
            h.symbol[offs[length[sym]]++] = short(sym);
    return left;
}

class Inflater {
public:
    Inflater(const uint8_t* in, size_t len, QByteArray& out, qsizetype outStart)
        : m_in(in), m_len(len), m_out(out), m_outStart(outStart)
    {
    }

    // 0 on success, after which consumed() is the byte length of the stream.
    int run()
    {
        int last = 0;
        do {
            last = bits(1);
            const int type = bits(2);
            if (m_err)
                return -1;
            int r = -1;
            if (type == 0)
                r = stored();
            else if (type == 1)
                r = fixed();
            else if (type == 2)
                r = dynamic();
            if (r != 0)
                return r;
        } while (!last);
        m_bitbuf = 0;   // the rest of the last byte is padding
        m_bitcnt = 0;
        return 0;
    }
    size_t consumed() const { return m_pos; }

private:
    int bits(int need)
    {
        uint32_t val = m_bitbuf;
        while (m_bitcnt < need) {
            if (m_pos >= m_len) {
                m_err = true;
                return 0;
            }
            val |= uint32_t(m_in[m_pos++]) << m_bitcnt;
            m_bitcnt += 8;
        }
        m_bitbuf = val >> need;
        m_bitcnt -= need;
        return int(val & ((1u << need) - 1));
    }

    int stored()
    {
        m_bitbuf = 0;
        m_bitcnt = 0;
        if (m_len - m_pos < 4)
            return -1;
        const unsigned len = m_in[m_pos] | (unsigned(m_in[m_pos + 1]) << 8);
        const unsigned nlen = m_in[m_pos + 2] | (unsigned(m_in[m_pos + 3]) << 8);
        m_pos += 4;
        if (len != (~nlen & 0xFFFFu) || m_len - m_pos < len)
            return -1;
        if (m_out.size() + qsizetype(len) > kMaxMessage)
            return -1;
        m_out.append(reinterpret_cast<const char*>(m_in + m_pos), qsizetype(len));
        m_pos += len;
        return 0;
    }

    int decode(const Huffman& h)
    {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= bits(1);
            if (m_err)
                return -1;
            const int count = h.count[len];
            if (code - count < first)
                return h.symbol[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }

    int codes(const Huffman& lencode, const Huffman& distcode)
    {
        static constexpr short lbase[29] = {3,  4,  5,  6,  7,  8,  9,  10,  11,  13,  15,  17,  19,  23, 27,
                                            31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static constexpr short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                           2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static constexpr int dbase[30] = {1,    2,    3,    4,    5,    7,     9,     13,    17,  25,
                                          33,   49,   65,   97,   129,  193,   257,   385,   513, 769,
                                          1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static constexpr short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                           6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int sym = decode(lencode);
            if (sym < 0)
                return -1;
            if (sym < 256) {
                m_out.append(char(sym));
            } else if (sym == 256) {
                return 0;
            } else {
                sym -= 257;
                if (sym >= 29)
                    return -1;
                const int len = lbase[sym] + bits(lext[sym]);
                const int dsym = decode(distcode);
                if (dsym < 0 || dsym >= 30)
                    return -1;
                const int dist = dbase[dsym] + bits(dext[dsym]);
                if (m_err || dist > m_out.size() - m_outStart)
                    return -1;
                const qsizetype at = m_out.size();
                if (at + len > kMaxMessage)
                    return -1;
                m_out.resize(at + len);
                char* d = m_out.data();
                for (int i = 0; i < len; ++i)   // byte-wise: source may overlap
                    d[at + i] = d[at - dist + i];
            }
            if (m_out.size() > kMaxMessage)
                return -1;
        }
    }

    int fixed()
    {
        static const std::array<Huffman, 2> tables = [] {
            std::array<Huffman, 2> t{};
            short lengths[288];
            int sym = 0;
            for (; sym < 144; ++sym)
                lengths[sym] = 8;
            for (; sym < 256; ++sym)
                lengths[sym] = 9;
            for (; sym < 280; ++sym)
                lengths[sym] = 7;
            for (; sym < 288; ++sym)
                lengths[sym] = 8;
            construct(t[0], lengths, 288);
            for (sym = 0; sym < 30; ++sym)
                lengths[sym] = 5;
            construct(t[1], lengths, 30);
            return t;
        }();
        return codes(tables[0], tables[1]);
    }

    int dynamic()
    {
        static constexpr short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        const int nlen = bits(5) + 257;
        const int ndist = bits(5) + 1;
        const int ncode = bits(4) + 4;
        if (m_err || nlen > 286 || ndist > 30)
            return -1;
        short lengths[286 + 30];
        int index = 0;
        for (; index < ncode; ++index)
            lengths[order[index]] = short(bits(3));
        for (; index < 19; ++index)
            lengths[order[index]] = 0;
        if (m_err)
            return -1;
        Huffman lencode, distcode;
        if (construct(lencode, lengths, 19) != 0)
            return -1;
        index = 0;
        while (index < nlen + ndist) {
            const int sym = decode(lencode);
            if (sym < 0)
                return -1;
            if (sym < 16) {
                lengths[index++] = short(sym);
                continue;
            }
            short len = 0;
            int rep;
            if (sym == 16) {
                if (index == 0)
                    return -1;
                len = lengths[index - 1];
                rep = 3 + bits(2);
            } else if (sym == 17) {
                rep = 3 + bits(3);
            } else {
                rep = 11 + bits(7);
            }
            if (m_err || index + rep > nlen + ndist)
                return -1;
            while (rep--)
                lengths[index++] = len;
        }
        if (lengths[256] == 0)
            return -1;
        int err = construct(lencode, lengths, nlen);
        if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1))
            return -1;
        err = construct(distcode, lengths + nlen, ndist);
        if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1))
            return -1;
        return codes(lencode, distcode);
    }

    const uint8_t* m_in;
    size_t m_len;
    size_t m_pos = 0;
    uint32_t m_bitbuf = 0;
    int m_bitcnt = 0;
    bool m_err = false;
    QByteArray& m_out;
    qsizetype m_outStart;
};

const std::array<quint32, 256>& crcTable()
{
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> t{};
        for (quint32 n = 0; n < 256; ++n) {
            quint32 c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    return table;
}

quint32 crc32Raw(const char* data, qsizetype len)
{
    const auto& t = crcTable();
    quint32 c = 0xFFFFFFFFu;
    for (qsizetype i = 0; i < len; ++i)
        c = t[(c ^ uint8_t(data[i])) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

} // namespace

quint32 crc32(const QByteArray& data)
{
    return crc32Raw(data.constData(), data.size());
}

bool gunzip(const QByteArray& in, QByteArray* out, QString* error)
{
    auto fail = [error](const char* why) {
        if (error)
            *error = QString::fromLatin1(why);
        return false;
    };
    out->clear();
    const auto* p = reinterpret_cast<const uint8_t*>(in.constData());
    const size_t n = size_t(in.size());
    size_t pos = 0;
    int members = 0;
    while (pos < n) {
        if (members > 0 && p[pos] == 0) {   // trailing zero padding after a member
            bool allZero = true;
            for (size_t i = pos; i < n && allZero; ++i)
                allZero = p[i] == 0;
            if (allZero)
                break;
        }
        if (n - pos < 18)
            return fail("truncated gzip member");
        if (p[pos] != 0x1F || p[pos + 1] != 0x8B)
            return fail("not gzip data (bad magic)");
        if (p[pos + 2] != 8)
            return fail("unsupported gzip compression method");
        const uint8_t flags = p[pos + 3];
        if (flags & 0xE0)
            return fail("reserved gzip flags set");
        pos += 10;
        if (flags & 0x04) {   // FEXTRA
            if (n - pos < 2)
                return fail("truncated gzip header");
            const size_t xlen = p[pos] | (size_t(p[pos + 1]) << 8);
            pos += 2 + xlen;
        }
        for (uint8_t f : {uint8_t(0x08), uint8_t(0x10)}) {   // FNAME, FCOMMENT
            if (!(flags & f))
                continue;
            while (pos < n && p[pos] != 0)
                ++pos;
            ++pos;
        }
        if (flags & 0x02)   // FHCRC
            pos += 2;
        if (pos >= n)
            return fail("truncated gzip header");
        const qsizetype start = out->size();
        Inflater inf(p + pos, n - pos, *out, start);
        if (inf.run() != 0)
            return fail("corrupt or oversized deflate stream");
        pos += inf.consumed();
        if (n - pos < 8)
            return fail("truncated gzip trailer");
        const quint32 crc = qFromLittleEndian<quint32>(p + pos);
        const quint32 isize = qFromLittleEndian<quint32>(p + pos + 4);
        pos += 8;
        if (crc32Raw(out->constData() + start, out->size() - start) != crc)
            return fail("gzip CRC mismatch");
        if (isize != quint32(out->size() - start))
            return fail("gzip size mismatch");
        ++members;
    }
    if (members == 0)
        return fail("empty gzip data");
    return true;
}

QByteArray gzipStored(const QByteArray& data)
{
    QByteArray out;
    out.reserve(data.size() + data.size() / 65535 * 5 + 32);
    static constexpr char header[10] = {'\x1f', '\x8b', 8, 0, 0, 0, 0, 0, 0, '\xff'};
    out.append(header, 10);
    qsizetype pos = 0;
    do {
        const qsizetype len = qMin<qsizetype>(65535, data.size() - pos);
        const bool last = pos + len >= data.size();
        out.append(char(last ? 1 : 0));   // BFINAL, BTYPE=00 (stored)
        const quint16 l = quint16(len), nl = quint16(~l);
        out.append(char(l & 0xFF)).append(char(l >> 8));
        out.append(char(nl & 0xFF)).append(char(nl >> 8));
        out.append(data.constData() + pos, len);
        pos += len;
    } while (pos < data.size());
    char trailer[8];
    qToLittleEndian<quint32>(crc32(data), trailer);
    qToLittleEndian<quint32>(quint32(data.size()), trailer + 4);
    out.append(trailer, 8);
    return out;
}

QByteArray frameSmall(const QByteArray& payload)
{
    QByteArray out = headerLine(kHeaderSmall);
    appendLe32(out, qint32(payload.size()));
    out.append(payload);
    return out;
}

QByteArray frameChunked(const QByteArray& payload)
{
    const qsizetype total = payload.size();
    const qsizetype count = (total + kChunkSize - 1) / kChunkSize;
    QByteArray out = headerLine(kHeaderChunk);
    out.reserve(out.size() + 8 + total + 4 * count);
    appendLe32(out, qint32(total));
    appendLe32(out, qint32(count));
    for (qsizetype pos = 0; pos < total; pos += kChunkSize) {
        const qsizetype len = qMin(kChunkSize, total - pos);
        appendLe32(out, qint32(len));
        out.append(payload.constData() + pos, len);
    }
    return out;
}

QByteArray frameCompressed(const QByteArray& payload)
{
    const QByteArray z = gzipStored(payload);
    QByteArray out = headerLine(kHeaderCompressed);
    appendLe32(out, qint32(z.size()));
    out.append(z);
    return out;
}

QByteArray frame(const QByteArray& payload)
{
    if (payload.size() >= kCompressThreshold)
        return frameCompressed(payload);
    if (payload.size() >= kChunkThreshold)
        return frameChunked(payload);
    return frameSmall(payload);
}

void FrameDecoder::resync(qsizetype from)
{
    const qsizetype at = m_buf.indexOf("FDG_", from);
    if (at >= 0)
        m_buf.remove(0, at);
    else
        m_buf = m_buf.right(qMin<qsizetype>(3, m_buf.size()));   // "FDG_" may be split
}

FrameDecoder::Status FrameDecoder::next(QByteArray* payload, QString* kind, QString* error)
{
    if (m_buf.isEmpty())
        return NeedMore;
    const qsizetype nl = m_buf.indexOf('\n');
    if (nl < 0) {
        if (m_buf.size() > 256 || (m_buf.size() >= 4 && !m_buf.startsWith("FDG_"))) {
            *error = QStringLiteral("no protocol header, skipping %1 bytes").arg(m_buf.size());
            resync(1);
            return Error;
        }
        return NeedMore;
    }
    const QByteArray header = m_buf.left(nl).trimmed();
    if (!header.startsWith("FDG_")) {
        *error = QStringLiteral("invalid header \"%1\"").arg(QString::fromLatin1(header.left(40)));
        resync(1);
        return Error;
    }
    const qsizetype bodyAt = nl + 1;
    const char* body = m_buf.constData() + bodyAt;
    const qsizetype avail = m_buf.size() - bodyAt;

    if (header.startsWith(kHeaderCompressed)) {
        if (avail < 4)
            return NeedMore;
        const qint32 len = readLe32(body);
        if (len <= 0 || len > kMaxMessage) {
            *error = QStringLiteral("invalid compressed length %1").arg(len);
            resync(bodyAt);
            return Error;
        }
        if (avail < 4 + qsizetype(len))
            return NeedMore;
        const QByteArray z(body + 4, len);
        m_buf.remove(0, bodyAt + 4 + len);
        QString why;
        if (!gunzip(z, payload, &why)) {
            *error = QStringLiteral("cannot decompress message: %1").arg(why);
            return Error;
        }
        *kind = QStringLiteral("compressed");
        return Message;
    }

    if (header.startsWith(kHeaderChunk)) {
        if (avail < 8)
            return NeedMore;
        const qint32 total = readLe32(body);
        const qint32 count = readLe32(body + 4);
        if (total < 0 || total > kMaxMessage || count < 0 || count > kMaxMessage) {
            *error = QStringLiteral("invalid chunk header (total %1, count %2)").arg(total).arg(count);
            resync(bodyAt);
            return Error;
        }
        qsizetype off = 8, sum = 0;
        for (qint32 i = 0; i < count; ++i) {   // check every chunk has arrived before copying
            if (avail < off + 4)
                return NeedMore;
            const qint32 len = readLe32(body + off);
            if (len < 0 || len > kMaxMessage) {
                *error = QStringLiteral("invalid chunk %1 length %2").arg(i).arg(len);
                resync(bodyAt);
                return Error;
            }
            off += 4;
            if (avail < off + len)
                return NeedMore;
            off += len;
            sum += len;
        }
        if (sum != total) {
            *error = QStringLiteral("chunk sizes add up to %1, header says %2").arg(sum).arg(total);
            m_buf.remove(0, bodyAt + off);
            return Error;
        }
        payload->clear();
        payload->reserve(total);
        off = 8;
        for (qint32 i = 0; i < count; ++i) {
            const qint32 len = readLe32(body + off);
            payload->append(body + off + 4, len);
            off += 4 + len;
        }
        m_buf.remove(0, bodyAt + off);
        *kind = QStringLiteral("chunked");
        return Message;
    }

    // FDG_PROTOCOL, and like Space Station any other FDG_ header.
    if (avail < 4)
        return NeedMore;
    const qint32 len = readLe32(body);
    if (len <= 0 || len > kMaxMessage) {
        *error = QStringLiteral("invalid message length %1").arg(len);
        resync(bodyAt);
        return Error;
    }
    if (avail < 4 + qsizetype(len))
        return NeedMore;
    *payload = QByteArray(body + 4, len);
    m_buf.remove(0, bodyAt + 4 + len);
    *kind = QStringLiteral("small");
    return Message;
}

} // namespace fg::fdg
