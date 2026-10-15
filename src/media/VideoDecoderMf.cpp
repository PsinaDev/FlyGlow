#include "MediaInternal.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRect>
#include <QTransform>

#ifdef Q_OS_WIN

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace fg::media::detail {

namespace {

using Microsoft::WRL::ComPtr;

constexpr DWORD kVideoStream = DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

class MfSession {
public:
    MfSession()
    {
        // S_FALSE still needs a matching CoUninitialize; RPC_E_CHANGED_MODE (an STA thread) must not get one.
        m_com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        m_mf = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
    }
    ~MfSession()
    {
        if (m_mf)
            MFShutdown();
        if (m_com)
            CoUninitialize();
    }
    MfSession(const MfSession&) = delete;
    MfSession& operator=(const MfSession&) = delete;

    bool ok() const { return m_mf; }

private:
    bool m_com = false;
    bool m_mf = false;
};

struct VideoFormat {
    int width = 0;
    int height = 0;
    LONG stride = 0;
    QRect crop;
    double pixelAspect = 1.0;
};

bool readFormat(IMFSourceReader* reader, VideoFormat& f)
{
    ComPtr<IMFMediaType> type;
    if (FAILED(reader->GetCurrentMediaType(kVideoStream, &type)))
        return false;
    UINT32 w = 0, h = 0;
    if (FAILED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h)) || w == 0 || h == 0)
        return false;
    f.width = int(w);
    f.height = int(h);

    UINT32 stride = 0;
    // The attribute is a UINT32 holding a signed value; negative means bottom-up rows.
    f.stride = SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)) ? LONG(INT32(stride)) : LONG(w) * 4;

    f.crop = QRect(0, 0, f.width, f.height);
    MFVideoArea area{};
    if (SUCCEEDED(type->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&area), sizeof(area), nullptr))
        || SUCCEEDED(type->GetBlob(MF_MT_GEOMETRIC_APERTURE, reinterpret_cast<UINT8*>(&area), sizeof(area), nullptr))) {
        const QRect r = QRect(area.OffsetX.value, area.OffsetY.value, int(area.Area.cx), int(area.Area.cy)) & f.crop;
        if (r.width() > 0 && r.height() > 0)
            f.crop = r;
    }

    UINT32 num = 1, den = 1;
    f.pixelAspect = SUCCEEDED(MFGetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, &num, &den)) && num && den
                        ? double(num) / den
                        : 1.0;
    return true;
}

QImage toImage(const BYTE* scan0, LONG pitch, const VideoFormat& f, int maxSide, int rotation)
{
    const int w = f.crop.width(), h = f.crop.height();
    const BYTE* top = scan0 + qptrdiff(f.crop.y()) * pitch + qptrdiff(f.crop.x()) * 4;
    // QImage cannot take a negative stride, so bottom-up frames are wrapped upside down and flipped after scaling.
    const BYTE* base = pitch < 0 ? top + qptrdiff(h - 1) * pitch : top;
    const QImage wrapped(base, w, h, qsizetype(std::abs(pitch)), QImage::Format_RGB32);

    QSize target(std::max(1, int(std::lround(w * f.pixelAspect))), h);
    if (maxSide > 0 && (target.width() > maxSide || target.height() > maxSide))
        target = target.scaled(maxSide, maxSide, Qt::KeepAspectRatio);
    QImage img = target == wrapped.size() ? wrapped.copy()
                                          : wrapped.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (pitch < 0)
        img.flip(Qt::Vertical);

    // The padding byte of MF's RGB32 is not guaranteed to be 0xff.
    for (int y = 0; y < img.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x)
            line[x] |= 0xff000000u;
    }

    if (rotation != 0)
        img = img.transformed(QTransform().rotate(rotation));
    return img;
}

QImage sampleToImage(IMFSample* sample, const VideoFormat& f, int maxSide, int rotation)
{
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample->ConvertToContiguousBuffer(&buffer)))
        return {};
    QImage img;
    ComPtr<IMF2DBuffer> buffer2d;
    BYTE* scan0 = nullptr;
    LONG pitch = 0;
    if (SUCCEEDED(buffer.As(&buffer2d)) && SUCCEEDED(buffer2d->Lock2D(&scan0, &pitch))) {
        img = toImage(scan0, pitch, f, maxSide, rotation);
        buffer2d->Unlock2D();
        return img;
    }
    BYTE* data = nullptr;
    DWORD maxLength = 0, length = 0;
    if (FAILED(buffer->Lock(&data, &maxLength, &length)))
        return {};
    const qint64 needed = qint64(std::abs(f.stride)) * f.height;
    if (length >= needed) {
        const BYTE* first = f.stride < 0 ? data + qptrdiff(f.height - 1) * std::abs(f.stride) : data;
        img = toImage(first, f.stride, f, maxSide, rotation);
    }
    buffer->Unlock();
    return img;
}

HRESULT openReader(const QString& path, IMFAttributes* attributes, ComPtr<IMFSourceReader>& reader)
{
    const QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    const HRESULT hr = MFCreateSourceReaderFromURL(reinterpret_cast<LPCWSTR>(native.utf16()), attributes, &reader);
    if (SUCCEEDED(hr) || native.size() < MAX_PATH - 12)
        return hr;

    // The URL resolver stops at MAX_PATH; a byte stream opened with the \\?\ prefix does not.
    const QString extended = native.startsWith(QLatin1String("\\\\")) ? QLatin1String("\\\\?\\UNC\\") + native.mid(2)
                                                                     : QLatin1String("\\\\?\\") + native;
    ComPtr<IMFByteStream> stream;
    if (FAILED(MFCreateFile(MF_ACCESSMODE_READ, MF_OPENMODE_FAIL_IF_NOT_EXIST, MF_FILEFLAGS_NONE,
                            reinterpret_cast<LPCWSTR>(extended.utf16()), &stream)))
        return hr;
    ComPtr<IMFSourceResolver> resolver;
    if (FAILED(MFCreateSourceResolver(&resolver)))
        return hr;
    // The resolver picks a container handler from the URL's extension, so pass the bare file name.
    const QString hint = QFileInfo(path).fileName();
    MF_OBJECT_TYPE type = MF_OBJECT_INVALID;
    ComPtr<IUnknown> object;
    if (FAILED(resolver->CreateObjectFromByteStream(
            stream.Get(), reinterpret_cast<LPCWSTR>(hint.utf16()),
            MF_RESOLUTION_MEDIASOURCE | MF_RESOLUTION_CONTENT_DOES_NOT_HAVE_TO_MATCH_EXTENSION_OR_MIME_TYPE, nullptr,
            &type, &object)))
        return hr;
    ComPtr<IMFMediaSource> source;
    if (FAILED(object.As(&source)))
        return hr;
    const HRESULT fromSource = MFCreateSourceReaderFromMediaSource(source.Get(), attributes, &reader);
    if (FAILED(fromSource))
        source->Shutdown();
    return fromSource;
}

double readDuration(IMFSourceReader* reader)
{
    PROPVARIANT var;
    PropVariantInit(&var);
    double seconds = 0;
    if (SUCCEEDED(reader->GetPresentationAttribute(DWORD(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &var))
        && var.vt == VT_UI8)
        seconds = double(var.uhVal.QuadPart) / 1e7;
    PropVariantClear(&var);
    return seconds;
}

} // namespace

DecodedClip decodeVideo(const QString& path, const DecodeOptions& options, const ProgressFn& progress)
{
    DecodedClip clip;
    clip.isVideo = true;

    // Declared first so it outlives every COM pointer below: MFShutdown must come last.
    MfSession session;
    if (!session.ok()) {
        clip.error = QCoreApplication::translate("MediaDecoder", "Media Foundation is not available on this system.");
        return clip;
    }

    ComPtr<IMFAttributes> attributes;
    if (FAILED(MFCreateAttributes(&attributes, 1))) {
        clip.error = QCoreApplication::translate("MediaDecoder", "Media Foundation is not available on this system.");
        return clip;
    }
    attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

    ComPtr<IMFSourceReader> reader;
    if (const HRESULT hr = openReader(path, attributes.Get(), reader); FAILED(hr)) {
        clip.error = QCoreApplication::translate("MediaDecoder", "The file could not be opened as a video (error 0x%1).")
                         .arg(quint32(hr), 8, 16, QChar('0'));
        return clip;
    }
    reader->SetStreamSelection(DWORD(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    if (FAILED(reader->SetStreamSelection(kVideoStream, TRUE))) {
        clip.error = QCoreApplication::translate("MediaDecoder", "The file has no video track.");
        return clip;
    }

    int rotation = 0;
    {
        ComPtr<IMFMediaType> native;
        if (SUCCEEDED(reader->GetNativeMediaType(kVideoStream, 0, &native))) {
            UINT32 num = 0, den = 0;
            if (SUCCEEDED(MFGetAttributeRatio(native.Get(), MF_MT_FRAME_RATE, &num, &den)) && num && den)
                clip.sourceFps = double(num) / den;
            UINT32 rot = 0;
            // The value says how far the stored picture was turned counter-clockwise; turning it back is clockwise.
            if (SUCCEEDED(native->GetUINT32(MF_MT_VIDEO_ROTATION, &rot)))
                rotation = int(rot % 360) / 90 * 90;
        }
    }

    ComPtr<IMFMediaType> outType;
    if (FAILED(MFCreateMediaType(&outType))) {
        clip.error = QCoreApplication::translate("MediaDecoder", "Media Foundation is not available on this system.");
        return clip;
    }
    outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    VideoFormat format;
    if (FAILED(reader->SetCurrentMediaType(kVideoStream, nullptr, outType.Get())) || !readFormat(reader.Get(), format)) {
        clip.error = QCoreApplication::translate("MediaDecoder", "No decoder for this video format is installed.");
        return clip;
    }
    clip.sourceDuration = readDuration(reader.Get());
    {
        const QSize stored(int(std::lround(format.crop.width() * format.pixelAspect)), format.crop.height());
        clip.sourceSize = rotation % 180 ? stored.transposed() : stored;
    }

    const double frameSeconds = clip.sourceFps > 0 ? 1.0 / clip.sourceFps : 0.0;
    FrameCollector collector(options, clip.sourceDuration, frameSeconds);
    if (collector.start() > 0) {
        PROPVARIANT pos;
        PropVariantInit(&pos);
        pos.vt = VT_I8;
        pos.hVal.QuadPart = LONGLONG(collector.start() * 1e7);
        reader->SetCurrentPosition(GUID_NULL, pos);
        PropVariantClear(&pos);
    }

    double end = options.trimEndSeconds > 0 ? options.trimEndSeconds : clip.sourceDuration;
    if (clip.sourceDuration > 0)
        end = std::min(end, clip.sourceDuration);
    const double span = end - collector.start();

    bool failed = false;
    for (;;) {
        DWORD stream = 0, flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        if (FAILED(reader->ReadSample(kVideoStream, 0, &stream, &flags, &timestamp, &sample))) {
            failed = true;
            break;
        }
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
            readFormat(reader.Get(), format);
        if (flags & MF_SOURCE_READERF_ERROR) {
            failed = true;
            break;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
            break;
        if (!sample)
            continue;

        const double t = double(timestamp) / 1e7;
        if (collector.pastEnd(t))
            break;
        LONGLONG duration = 0;
        double d = SUCCEEDED(sample->GetSampleDuration(&duration)) && duration > 0 ? double(duration) / 1e7
                                                                                   : frameSeconds;
        if (d <= 0)
            d = 1.0 / 30.0;
        if (progress && !progress(span > 0 ? std::clamp((t - collector.start()) / span, 0.0, 1.0) : -1.0)) {
            clip.cancelled = true;
            return clip;
        }
        if (!collector.offer(t, d))
            continue;
        QImage img = sampleToImage(sample.Get(), format, options.maxSide, rotation);
        if (img.isNull()) {
            const QSize size = clip.sourceSize.scaled(options.maxSide, options.maxSide, Qt::KeepAspectRatio);
            img = QImage(size.isEmpty() ? QSize(1, 1) : size, QImage::Format_RGB32);
            img.fill(Qt::black);
        }
        collector.keep(std::move(img));
    }

    collector.finish(clip);
    if (clip.frames.empty()) {
        clip.error = failed ? QCoreApplication::translate("MediaDecoder", "The video could not be decoded.")
                            : QCoreApplication::translate("MediaDecoder", "There are no frames in the selected range.");
    }
    return clip;
}

} // namespace fg::media::detail

#else

namespace fg::media::detail {

DecodedClip decodeVideo(const QString&, const DecodeOptions&, const ProgressFn&)
{
    DecodedClip clip;
    clip.error = QCoreApplication::translate("MediaDecoder", "Video import is only available on Windows.");
    return clip;
}

} // namespace fg::media::detail

#endif
