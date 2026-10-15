#pragma once

#include "MediaDecoder.h"

#include <QImage>
#include <vector>

namespace fg::media::detail {

/** Downscales to fit maxSide, never upscaling, and flattens alpha onto black. */
QImage fitFrame(const QImage& source, int maxSide);

/** Picks frames in one front-to-back pass and never holds more than twice the frame budget. */
class FrameCollector {
public:
    /** knownDuration and frameSeconds are 0 when the source cannot tell in advance. */
    FrameCollector(const DecodeOptions& options, double knownDuration, double frameSeconds);

    double start() const { return m_start; }
    bool pastEnd(double t) const { return t >= m_end || (m_slots && t >= m_start + m_max * m_slotLength); }

    /** Offers a frame shown from t for d seconds; true means convert it and pass it to keep() next. */
    bool offer(double t, double d);
    void keep(QImage frame);

    void finish(DecodedClip& clip);

private:
    struct Item {
        QImage image;
        double t = 0;
    };

    double m_start = 0;
    double m_end = 0;
    int m_max = 200;
    bool m_slots = false;
    double m_slotLength = 0;
    int m_nextSlot = 0;
    int m_stride = 1;
    long long m_counter = 0;
    double m_pendingT = 0;
    double m_lastEnd = 0;
    std::vector<Item> m_items;
};

DecodedClip decodeVideo(const QString& path, const DecodeOptions& options, const ProgressFn& progress);

/** Qt reads only the first APNG frame. Returns false and leaves clip alone for a non-animated PNG. */
bool decodeApng(const QString& path, const DecodeOptions& options, const ProgressFn& progress, DecodedClip& clip);

} // namespace fg::media::detail
