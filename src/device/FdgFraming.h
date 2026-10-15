#pragma once

#include <QByteArray>
#include <QString>

namespace fg::fdg {

inline constexpr char kHeaderSmall[] = "FDG_PROTOCOL";
inline constexpr char kHeaderChunk[] = "FDG_CHUNK";
inline constexpr char kHeaderCompressed[] = "FDG_COMPRESSED";
inline constexpr qsizetype kChunkThreshold = 1024 * 1024;
inline constexpr qsizetype kCompressThreshold = 5 * 1024 * 1024;
inline constexpr qsizetype kChunkSize = 64 * 1024;
/** Same sanity cap as Space Station. */
inline constexpr qsizetype kMaxMessage = 100 * 1024 * 1024;

/**
 * Frames a protobuf payload for `\\.\pipe\fcs.sock` exactly as Space Station's client does, choosing
 * small, chunked or gzip framing by size; see docs/ipc-protocol.md.
 */
QByteArray frame(const QByteArray& payload);

QByteArray frameSmall(const QByteArray& payload);
QByteArray frameChunked(const QByteArray& payload);
QByteArray frameCompressed(const QByteArray& payload);

/** Incremental decoder for a framed byte stream; skips garbage by resyncing on the next "FDG_", as Space Station does. */
class FrameDecoder {
public:
    void append(const QByteArray& data) { m_buf.append(data); }
    void clear() { m_buf.clear(); }
    qsizetype buffered() const { return m_buf.size(); }

    enum Status { NeedMore, Message, Error };
    /**
     * On Message, payload and kind ("small", "chunked" or "compressed") are set. On Error, error gets
     * an English diagnostic and the bad bytes are already dropped, so next() can simply be called again.
     */
    Status next(QByteArray* payload, QString* kind, QString* error);

private:
    void resync(qsizetype from);
    QByteArray m_buf;
};

/** RFC 1952 gzip with any deflate block type and concatenated members; output is capped at kMaxMessage. */
bool gunzip(const QByteArray& in, QByteArray* out, QString* error);
/** Valid gzip built from stored (uncompressed) deflate blocks. */
QByteArray gzipStored(const QByteArray& data);
quint32 crc32(const QByteArray& data);

} // namespace fg::fdg
