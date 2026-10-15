#pragma once

#include <QColor>
#include <QMetaType>
#include <QVariant>
#include <QVector>

namespace fg {

/** Linear light with sRGB primaries, nominally 0..1 per channel. */
struct Rgb {
    float r = 0, g = 0, b = 0;
};

/** Linear-light colour with straight (not premultiplied) alpha. */
struct Rgba {
    float r = 0, g = 0, b = 0, a = 0;
    Rgba() = default;
    Rgba(float r_, float g_, float b_, float a_ = 1.f) : r(r_), g(g_), b(b_), a(a_) {}
    Rgba(const Rgb& c, float a_ = 1.f) : r(c.r), g(c.g), b(c.b), a(a_) {}
    Rgb rgb() const { return {r, g, b}; }
};

/** OKLab coordinates. */
struct Lab {
    float L = 0, a = 0, b = 0;
};

namespace color {

float srgbToLinear(float v);
float linearToSrgb(float v);

/** sRGB to linear light. */
Rgb fromQColor(const QColor& c);
/** Linear light to sRGB, clamped to 0..1. */
QColor toQColor(const Rgb& c);
QColor toQColor(const Rgba& c);

Lab toOklab(const Rgb& c);
Rgb fromOklab(const Lab& c);

/** Mixes in OKLab; t = 0 gives a, t = 1 gives b. */
Rgb mix(const Rgb& a, const Rgb& b, float t);
/** OKLab mix weighted by alpha, so fading from transparent does not drag the hue through black. */
Rgba mix(const Rgba& a, const Rgba& b, float t);

/** Perceived lightness 0..1 (OKLab-like L) to a linear intensity factor; fading by it looks even. */
inline float perceptualToLinear(float x) { return x <= 0.f ? 0.f : x * x * x; }
/** Inverse of perceptualToLinear(). */
float linearToPerceptual(float x);

/** Scales a linear colour by a perceived brightness 0..1. */
Rgb dim(const Rgb& c, float perceived);

/** Cyclic rainbow for h in [0, 1) whose hue sweeps at an even perceptual pace. */
Rgb rainbow(float h);

Rgb clamp01(const Rgb& c);
/** Rec. 709 relative luminance of a linear colour. */
float luminance(const Rgb& c);

} // namespace color

struct GradientStop {
    double pos = 0;   /**< 0..1 */
    QColor color;     /**< sRGB */
    bool operator==(const GradientStop& o) const { return pos == o.pos && color == o.color; }
};

/** Multi-stop gradient sampled in OKLab; stops are kept sorted by position and never empty. */
class ColorGradient {
public:
    ColorGradient();
    explicit ColorGradient(QVector<GradientStop> stops);
    ColorGradient(std::initializer_list<QColor> evenlySpaced);

    static ColorGradient rainbow();

    const QVector<GradientStop>& stops() const { return m_stops; }
    void setStops(QVector<GradientStop> stops);

    /** @param cyclic The last stop blends back into the first across the wrap. */
    Rgb sample(double t, bool cyclic = false) const;

    bool operator==(const ColorGradient& o) const { return m_stops == o.m_stops; }

    /** QVariantList of {"pos": double, "color": "#rrggbb"}. */
    QVariant toVariant() const;
    static ColorGradient fromVariant(const QVariant& v);

private:
    void rebuild();
    QVector<GradientStop> m_stops;
    QVector<Lab> m_lab;
};

} // namespace fg

Q_DECLARE_METATYPE(fg::ColorGradient)
