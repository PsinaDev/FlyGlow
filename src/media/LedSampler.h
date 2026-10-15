#pragma once

#include "core/Color.h"

#include <QImage>
#include <QPointF>
#include <QSize>
#include <QTransform>
#include <vector>

namespace fg {
class LedLayout;
}

namespace fg::media {

enum class FitMode { Cover = 0, Contain = 1, Stretch = 2 };

/**
 * Where the dock outline's bounding box lands on a frame. Positions are in
 * "view" space: the frame after rotation and flip, in pixels.
 */
struct Framing {
    FitMode fit = FitMode::Cover;
    /** 1 = the fitted size; 2 shows half as much of the image on the dock. */
    double zoom = 1.0;
    /** Dock centre offset from the view centre, as fractions of the view width and height. */
    double panX = 0;
    double panY = 0;
    /** Clockwise degrees: 0, 90, 180 or 270. */
    int rotation = 0;
    bool flip = false;
};

struct Adjustments {
    /** 1 = unchanged; scales roughly like an sRGB brightness slider. */
    double brightness = 1.0;
    /** 0 = grey, 1 = unchanged. */
    double saturation = 1.0;
};

QSize viewSize(const QSize& source, int rotation);
QTransform sourceToView(const QSize& source, const Framing& framing);
/** Layout units to view pixels. */
QTransform layoutToView(const LedLayout& layout, const QSize& source, const Framing& framing);

class LedSampler {
public:
    explicit LedSampler(const LedLayout& layout);

    /** Averages each LED's hex cell in linear light; `out` gets one colour per LED. Thread-safe. */
    void sample(const QImage& frame, const Framing& framing, const Adjustments& adjust,
                std::vector<Rgb>& out) const;
    /** Blocks while the frames are sampled in parallel on the global thread pool. */
    std::vector<std::vector<Rgb>> sampleAll(const std::vector<QImage>& frames, const Framing& framing,
                                            const Adjustments& adjust) const;

private:
    const LedLayout& m_layout;
    std::vector<QPointF> m_pattern;
};

} // namespace fg::media
