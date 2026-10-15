#include "LedSampler.h"

#include "core/LedLayout.h"

#include <QtConcurrent/QtConcurrentMap>
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace fg::media {

namespace {

constexpr double kPatternStep = 0.25;

const std::array<float, 256>& srgbLut()
{
    static const std::array<float, 256> lut = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i)
            t[size_t(i)] = color::srgbToLinear(i / 255.f);
        return t;
    }();
    return lut;
}

struct LinearPatch {
    int x0 = 0, y0 = 0, k = 1, w = 0, h = 0;
    std::vector<float> rgb;

    void build(const QImage& img, const QRect& region, int factor)
    {
        k = factor;
        x0 = region.left() / k * k;
        y0 = region.top() / k * k;
        const int x1 = std::min(img.width(), (region.right() + 1 + k - 1) / k * k);
        const int y1 = std::min(img.height(), (region.bottom() + 1 + k - 1) / k * k);
        w = (x1 - x0 + k - 1) / k;
        h = (y1 - y0 + k - 1) / k;
        rgb.assign(size_t(w) * size_t(h) * 3, 0.f);
        std::vector<int> count(size_t(w) * size_t(h), 0);
        const auto& lut = srgbLut();
        for (int y = y0; y < y1; ++y) {
            const auto* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            const size_t row = size_t((y - y0) / k) * size_t(w);
            for (int x = x0; x < x1; ++x) {
                const size_t t = row + size_t((x - x0) / k);
                const QRgb px = line[x];
                rgb[t * 3 + 0] += lut[size_t(qRed(px))];
                rgb[t * 3 + 1] += lut[size_t(qGreen(px))];
                rgb[t * 3 + 2] += lut[size_t(qBlue(px))];
                ++count[t];
            }
        }
        for (size_t t = 0; t < count.size(); ++t) {
            if (count[t] > 0) {
                const float inv = 1.f / float(count[t]);
                rgb[t * 3 + 0] *= inv;
                rgb[t * 3 + 1] *= inv;
                rgb[t * 3 + 2] *= inv;
            }
        }
    }

    // Points outside the patch are outside the image too, so they read as black.
    void sample(double sx, double sy, float acc[3]) const
    {
        const double u = (sx - x0) / k - 0.5;
        const double v = (sy - y0) / k - 0.5;
        const int i0 = int(std::floor(u)), j0 = int(std::floor(v));
        const float fu = float(u - i0), fv = float(v - j0);
        const float wts[4] = {(1 - fu) * (1 - fv), fu * (1 - fv), (1 - fu) * fv, fu * fv};
        const int is[4] = {i0, i0 + 1, i0, i0 + 1};
        const int js[4] = {j0, j0, j0 + 1, j0 + 1};
        for (int n = 0; n < 4; ++n) {
            if (is[n] < 0 || is[n] >= w || js[n] < 0 || js[n] >= h)
                continue;
            const float* p = &rgb[(size_t(js[n]) * size_t(w) + size_t(is[n])) * 3];
            acc[0] += p[0] * wts[n];
            acc[1] += p[1] * wts[n];
            acc[2] += p[2] * wts[n];
        }
    }
};

Rgb applyAdjustments(Rgb c, const Adjustments& a)
{
    if (std::abs(a.saturation - 1.0) > 1e-4) {
        Lab lab = color::toOklab(c);
        lab.a *= float(a.saturation);
        lab.b *= float(a.saturation);
        c = color::fromOklab(lab);
    }
    if (std::abs(a.brightness - 1.0) > 1e-4) {
        const float gain = float(std::pow(std::max(0.0, a.brightness), 2.2));
        c = {c.r * gain, c.g * gain, c.b * gain};
        const float peak = std::max({c.r, c.g, c.b});
        if (peak > 1.f)
            c = {c.r / peak, c.g / peak, c.b / peak};
    }
    return c;
}

} // namespace

QSize viewSize(const QSize& source, int rotation)
{
    return rotation % 180 ? source.transposed() : source;
}

QTransform sourceToView(const QSize& source, const Framing& framing)
{
    const double w = source.width(), h = source.height();
    QTransform rot;
    switch ((framing.rotation % 360 + 360) % 360) {
    case 90:
        rot = QTransform(0, 1, -1, 0, h, 0);
        break;
    case 180:
        rot = QTransform(-1, 0, 0, -1, w, h);
        break;
    case 270:
        rot = QTransform(0, -1, 1, 0, 0, w);
        break;
    default:
        break;
    }
    if (!framing.flip)
        return rot;
    const QSize v = viewSize(source, framing.rotation);
    return rot * QTransform(-1, 0, 0, 1, v.width(), 0);
}

QTransform layoutToView(const LedLayout& layout, const QSize& source, const Framing& framing)
{
    const QRectF box = layout.outline().boundingRect();
    const QSizeF v = viewSize(source, framing.rotation);
    const double fx = v.width() / box.width(), fy = v.height() / box.height();
    double sx = fx, sy = fy;
    if (framing.fit == FitMode::Cover)
        sx = sy = std::min(fx, fy);
    else if (framing.fit == FitMode::Contain)
        sx = sy = std::max(fx, fy);
    const double zoom = std::max(0.01, framing.zoom);
    QTransform t;
    t.translate(v.width() * (0.5 + framing.panX), v.height() * (0.5 + framing.panY));
    t.scale(sx / zoom, sy / zoom);
    t.translate(-box.center().x(), -box.center().y());
    return t;
}

LedSampler::LedSampler(const LedLayout& layout) : m_layout(layout)
{
    // A 19-point hex patch turned 30° against the LED lattice keeps its outer ring inside the LED's own cell.
    const QPointF e1(kPatternStep * std::sqrt(3.0) / 2.0, kPatternStep * 0.5);
    const QPointF e2(0, kPatternStep);
    for (int q = -2; q <= 2; ++q)
        for (int r = -2; r <= 2; ++r)
            if (std::abs(q + r) <= 2)
                m_pattern.push_back(e1 * q + e2 * r);
}

void LedSampler::sample(const QImage& frame, const Framing& framing, const Adjustments& adjustments,
                        std::vector<Rgb>& out) const
{
    out.assign(size_t(m_layout.count()), Rgb{});
    if (frame.isNull())
        return;
    const QImage img = frame.format() == QImage::Format_RGB32 ? frame : frame.convertToFormat(QImage::Format_RGB32);

    const QTransform toSource = layoutToView(m_layout, img.size(), framing)
                                * sourceToView(img.size(), framing).inverted();
    const double pixelsPerUnit = std::sqrt(std::abs(toSource.determinant()));
    // Pre-average k x k blocks so neighbouring sample points' bilinear footprints touch.
    const int k = std::max(1, int(kPatternStep * pixelsPerUnit * 0.5));
    const QRect reach = toSource.mapRect(m_layout.bounds().adjusted(-1, -1, 1, 1)).toAlignedRect();
    const QRect region = reach.adjusted(-2 * k, -2 * k, 2 * k, 2 * k) & img.rect();
    if (region.isEmpty())
        return;

    LinearPatch patch;
    patch.build(img, region, k);

    const float inv = 1.f / float(m_pattern.size());
    for (const Led& led : m_layout.leds()) {
        float acc[3] = {0, 0, 0};
        for (const QPointF& p : m_pattern) {
            const QPointF s = toSource.map(led.pos + p);
            patch.sample(s.x(), s.y(), acc);
        }
        out[size_t(led.index)] = applyAdjustments(Rgb{acc[0] * inv, acc[1] * inv, acc[2] * inv}, adjustments);
    }
}

std::vector<std::vector<Rgb>> LedSampler::sampleAll(const std::vector<QImage>& frames, const Framing& framing,
                                                    const Adjustments& adjust) const
{
    std::vector<std::vector<Rgb>> out(frames.size());
    std::vector<int> indices(frames.size());
    std::iota(indices.begin(), indices.end(), 0);
    QtConcurrent::blockingMap(indices, [&](int i) { sample(frames[size_t(i)], framing, adjust, out[size_t(i)]); });
    return out;
}

} // namespace fg::media
