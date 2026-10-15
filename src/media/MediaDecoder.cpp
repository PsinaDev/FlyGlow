#include "MediaDecoder.h"

#include "MediaInternal.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace fg::media {

namespace detail {

QImage fitFrame(const QImage& source, int maxSide)
{
    QImage img = source;
    if (maxSide > 0 && (img.width() > maxSide || img.height() > maxSide))
        img = img.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (!img.hasAlphaChannel())
        return img.convertToFormat(QImage::Format_RGB32);
    QImage out(img.size(), QImage::Format_RGB32);
    out.fill(Qt::black);
    QPainter p(&out);
    p.drawImage(0, 0, img);
    p.end();
    return out;
}

FrameCollector::FrameCollector(const DecodeOptions& options, double knownDuration, double frameSeconds)
{
    m_start = std::max(0.0, options.trimStartSeconds);
    m_end = options.trimEndSeconds > 0 ? options.trimEndSeconds : std::numeric_limits<double>::infinity();
    m_max = std::max(1, options.maxFrames);
    const double end = knownDuration > 0 ? std::min(m_end, knownDuration) : m_end;
    const double span = end - m_start;
    if (std::isfinite(span) && span > 0 && frameSeconds > 0 && span / frameSeconds > m_max * 1.02) {
        m_slots = true;
        m_slotLength = span / m_max;
    }
}

bool FrameCollector::offer(double t, double d)
{
    const double a = std::max(t, m_start);
    const double b = std::min(t + d, m_end);
    if (b <= a)
        return false;

    if (m_slots) {
        m_lastEnd = std::min(b, m_start + m_max * m_slotLength);
        if (m_nextSlot >= m_max)
            return false;
        auto mid = [this](int k) { return m_start + (k + 0.5) * m_slotLength; };
        if (mid(m_nextSlot) >= b)
            return false;
        m_pendingT = m_start + m_nextSlot * m_slotLength;
        while (m_nextSlot < m_max && mid(m_nextSlot) < b)
            ++m_nextSlot;
        return true;
    }

    m_lastEnd = b;
    const bool take = m_counter % m_stride == 0;
    ++m_counter;
    if (take)
        m_pendingT = a;
    return take;
}

void FrameCollector::keep(QImage frame)
{
    m_items.push_back({std::move(frame), m_pendingT});
    if (m_slots || int(m_items.size()) < 2 * m_max)
        return;
    std::vector<Item> half;
    half.reserve(m_items.size() / 2 + 1);
    for (size_t i = 0; i < m_items.size(); i += 2)
        half.push_back(std::move(m_items[i]));
    m_items.swap(half);
    m_stride *= 2;
}

void FrameCollector::finish(DecodedClip& clip)
{
    const size_t n = m_items.size();
    if (n == 0)
        return;
    std::vector<double> durations(n);
    for (size_t i = 0; i < n; ++i) {
        const double next = i + 1 < n ? m_items[i + 1].t : m_lastEnd;
        durations[i] = std::max(0.001, next - m_items[i].t);
    }
    clip.resampled = m_slots || m_stride > 1;

    if (int(n) <= m_max) {
        for (Item& it : m_items)
            clip.frames.push_back(std::move(it.image));
        clip.durations = std::move(durations);
    } else {
        const double total = std::accumulate(durations.begin(), durations.end(), 0.0);
        for (int index : resampleIndices(durations, m_max))
            clip.frames.push_back(m_items[size_t(index)].image);
        clip.durations.assign(size_t(m_max), total / m_max);
        clip.resampled = true;
    }
    m_items.clear();
}

} // namespace detail

namespace {

QString unreadableImage(const QImageReader& reader)
{
    return QCoreApplication::translate("MediaDecoder", "The image could not be read: %1").arg(reader.errorString());
}

DecodedClip decodeImage(QImageReader& reader, const DecodeOptions& options, const ProgressFn& progress)
{
    DecodedClip clip;
    reader.setAutoTransform(true);
    clip.sourceSize = reader.size();
    const int count = reader.imageCount();
    const bool animated = reader.supportsAnimation() && count != 1;

    if (!animated) {
        const QSize full = reader.size();
        if (full.isValid() && options.maxSide > 0
            && std::max(full.width(), full.height()) > 2 * options.maxSide)
            reader.setScaledSize(full.scaled(2 * options.maxSide, 2 * options.maxSide, Qt::KeepAspectRatio));
        const QImage img = reader.read();
        if (img.isNull()) {
            clip.error = unreadableImage(reader);
            return clip;
        }
        if (!clip.sourceSize.isValid())
            clip.sourceSize = img.size();
        clip.frames.push_back(detail::fitFrame(img, options.maxSide));
        clip.durations.push_back(1.0);
        return clip;
    }

    detail::FrameCollector collector(options, 0, 0);
    double t = 0;
    int index = 0;
    constexpr int kMaxSourceFrames = 100000;
    while (index < kMaxSourceFrames && reader.canRead()) {
        const QImage img = reader.read();
        if (img.isNull())
            break;
        if (index == 0 && !clip.sourceSize.isValid())
            clip.sourceSize = img.size();
        // Browsers show GIF delays of 0 or 10 ms as 100 ms, and files rely on it.
        const int delayMs = reader.nextImageDelay();
        const double d = (delayMs <= 10 ? 100 : delayMs) / 1000.0;
        if (collector.offer(t, d))
            collector.keep(detail::fitFrame(img, options.maxSide));
        t += d;
        ++index;
        if (progress && !progress(count > 0 ? std::min(1.0, double(index) / count) : -1.0)) {
            clip.cancelled = true;
            return clip;
        }
    }
    if (index == 0) {
        clip.error = unreadableImage(reader);
        return clip;
    }
    clip.sourceDuration = index > 1 ? t : 0;
    collector.finish(clip);
    if (clip.frames.empty())
        clip.error = QCoreApplication::translate("MediaDecoder", "There are no frames in the selected range.");
    return clip;
}

} // namespace

double DecodedClip::totalSeconds() const
{
    return std::accumulate(durations.begin(), durations.end(), 0.0);
}

DecodedClip decodeFile(const QString& path, const DecodeOptions& options, const ProgressFn& progress)
{
    if (!QFileInfo(path).isFile()) {
        DecodedClip clip;
        clip.error = QCoreApplication::translate("MediaDecoder", "The file does not exist.");
        return clip;
    }
    {
        QImageReader reader(path);
        if (reader.canRead()) {
            if (reader.format() == "png") {
                DecodedClip clip;
                if (detail::decodeApng(path, options, progress, clip))
                    return clip;
            }
            return decodeImage(reader, options, progress);
        }
    }
    DecodedClip clip = detail::decodeVideo(path, options, progress);
    if (!clip.error.isEmpty() && !videoSuffixes().contains(QFileInfo(path).suffix().toLower()))
        clip.error = QCoreApplication::translate("MediaDecoder", "This file format is not supported.");
    return clip;
}

QStringList imageSuffixes()
{
    QStringList out;
    for (const QByteArray& f : QImageReader::supportedImageFormats())
        out << QString::fromLatin1(f).toLower();
    out.removeDuplicates();
    return out;
}

QStringList videoSuffixes()
{
    return {"mp4", "m4v", "mov", "avi", "wmv", "asf", "mkv", "webm", "3gp", "3g2",
            "mpg", "mpeg", "ts", "mts", "m2ts"};
}

std::vector<int> resampleIndices(const std::vector<double>& durations, int count)
{
    std::vector<int> out;
    if (count <= 0 || durations.empty())
        return out;
    const double total = std::accumulate(durations.begin(), durations.end(), 0.0);
    out.reserve(size_t(count));
    size_t i = 0;
    double frameStart = 0;
    for (int k = 0; k < count; ++k) {
        const double mid = (k + 0.5) * total / count;
        while (i + 1 < durations.size() && frameStart + durations[i] <= mid) {
            frameStart += durations[i];
            ++i;
        }
        out.push_back(int(i));
    }
    return out;
}

} // namespace fg::media
