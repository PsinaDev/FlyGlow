#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <cstdint>

namespace fg::pw {

enum WireType : int { Varint = 0, Fixed64 = 1, LengthDelimited = 2, Fixed32 = 5 };

/** proto3 writer with implicit presence: scalars equal to their default are skipped unless always is set, as protobuf-es does. */
class Writer {
public:
    QByteArray& bytes() { return m_buf; }
    const QByteArray& bytes() const { return m_buf; }

    void varint(uint64_t v)
    {
        while (v >= 0x80) {
            m_buf.append(char((v & 0x7F) | 0x80));
            v >>= 7;
        }
        m_buf.append(char(v));
    }
    void key(int field, WireType wt) { varint((uint64_t(uint32_t(field)) << 3) | uint32_t(wt)); }

    /** int32 or enum; negative values are sign-extended to 64 bits (10 bytes). */
    void int32(int field, int32_t v, bool always = false)
    {
        if (v == 0 && !always)
            return;
        key(field, Varint);
        varint(uint64_t(int64_t(v)));
    }
    void boolean(int field, bool v, bool always = false)
    {
        if (!v && !always)
            return;
        key(field, Varint);
        varint(v ? 1 : 0);
    }
    void bytesField(int field, QByteArrayView v, bool always = false)
    {
        if (v.isEmpty() && !always)
            return;
        key(field, LengthDelimited);
        varint(uint64_t(v.size()));
        m_buf.append(v.data(), v.size());
    }
    void string(int field, const QString& s, bool always = false)
    {
        bytesField(field, s.toUtf8(), always);
    }
    /** Always written, even when empty, as sub-messages, oneof members and repeated elements must be. */
    void message(int field, const QByteArray& encoded) { bytesField(field, encoded, true); }

private:
    QByteArray m_buf;
};

struct Field {
    int number = 0;
    int wireType = 0;
    uint64_t varint = 0;      /**< Varint, Fixed32 and Fixed64 values */
    QByteArrayView bytes;     /**< LengthDelimited value, viewing the reader's buffer */

    int32_t asInt32() const { return int32_t(int64_t(varint)); }
    bool asBool() const { return varint != 0; }
    QString asString() const { return QString::fromUtf8(bytes.data(), bytes.size()); }
};

/** Does not copy: data must outlive the reader and every Field it returns. */
class Reader {
public:
    explicit Reader(QByteArrayView data)
        : m_p(reinterpret_cast<const uint8_t*>(data.data())), m_end(m_p + data.size())
    {
    }

    /** False at the end of the buffer, or on malformed input, which also sets error(). */
    bool next(Field& f)
    {
        if (m_p >= m_end || m_error)
            return false;
        uint64_t key = 0;
        if (!readVarint(key))
            return fail();
        f.number = int(key >> 3);
        f.wireType = int(key & 7);
        f.varint = 0;
        f.bytes = {};
        if (f.number <= 0)
            return fail();
        switch (f.wireType) {
        case Varint:
            if (!readVarint(f.varint))
                return fail();
            return true;
        case Fixed64:
            return readFixed(f, 8);
        case Fixed32:
            return readFixed(f, 4);
        case LengthDelimited: {
            uint64_t len = 0;
            if (!readVarint(len) || len > uint64_t(m_end - m_p))
                return fail();
            f.bytes = QByteArrayView(reinterpret_cast<const char*>(m_p), qsizetype(len));
            m_p += len;
            return true;
        }
        default:   // groups (3, 4) are not used by this schema
            return fail();
        }
    }
    bool error() const { return m_error; }

private:
    bool readVarint(uint64_t& v)
    {
        v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (m_p >= m_end)
                return false;
            const uint8_t b = *m_p++;
            v |= uint64_t(b & 0x7F) << shift;
            if (!(b & 0x80))
                return true;
        }
        return false;
    }
    bool readFixed(Field& f, int n)
    {
        if (m_end - m_p < n)
            return fail();
        uint64_t v = 0;
        for (int i = 0; i < n; ++i)
            v |= uint64_t(m_p[i]) << (8 * i);
        m_p += n;
        f.varint = v;
        return true;
    }
    bool fail()
    {
        m_error = true;
        return false;
    }

    const uint8_t* m_p;
    const uint8_t* m_end;
    bool m_error = false;
};

} // namespace fg::pw
