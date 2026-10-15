#pragma once

#include "core/Effect.h"
#include "core/LedLayout.h"

#include <QColor>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <numbers>
#include <utility>
#include <vector>

namespace fg {

void registerBasicEffects(EffectRegistry& r);
void registerWaveEffects(EffectRegistry& r);
void registerNatureEffects(EffectRegistry& r);
void registerParticleEffects(EffectRegistry& r);

namespace fxi {

constexpr double kPi = std::numbers::pi;
constexpr double kTau = 2.0 * std::numbers::pi;

ParamDesc pFloat(const QString& id, const QString& label, double def, double min, double max,
                 double step = 0.01, const QString& suffix = {});
/** Float 0..100 with a "%" suffix. */
ParamDesc pPercent(const QString& id, const QString& label, double def);
ParamDesc pInt(const QString& id, const QString& label, int def, int min, int max,
               const QString& suffix = {});
ParamDesc pBool(const QString& id, const QString& label, bool def);
ParamDesc pColor(const QString& id, const QString& label, const QColor& def);
ParamDesc pGradient(const QString& id, const QString& label, const ColorGradient& def);
ParamDesc pChoice(const QString& id, const QString& label, const QStringList& choices, int def);
ParamDesc pAngle(double def, const QString& id = "angle",
                 const QString& label = QT_TRANSLATE_NOOP("Effects", "Direction"));
/** Int "cycles": whole repeats per loop. */
ParamDesc pCycles(int def, int min = 1, int max = 12);
/** Int "seed", 0..9999. */
ParamDesc pSeed(int def = 1);
/** Adds "transparent" (Bool) and "background" (Color) for overlay effects. */
void addOverlayParams(QList<ParamDesc>& list, const QColor& background);

/** N colours at k/N, so the wrap segment is as wide as the others. */
ColorGradient cyclicPalette(std::initializer_list<QColor> colors);
/** Colours spaced evenly over 0..1 inclusive. */
ColorGradient linearPalette(std::initializer_list<QColor> colors);
ColorGradient stopsPalette(std::initializer_list<std::pair<double, QColor>> stops);

inline double clamp01(double x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
inline double lerp(double a, double b, double t) { return a + (b - a) * t; }
inline double fract(double x) { return x - std::floor(x); }
inline double smooth(double e0, double e1, double x) { return fx::smooth(e0, e1, x); }
inline double gauss(double d, double sigma) { return std::exp(-0.5 * (d / sigma) * (d / sigma)); }
/** Modulo with a non-negative result. */
inline int imod(int a, int m) { return ((a % m) + m) % m; }
/** Modulo with a non-negative result. */
inline long long lmod(long long a, long long m) { return ((a % m) + m) % m; }
/** Triangle wave with period 2: 0 -> 1 -> 0. */
inline double tri(double s)
{
    const double m = s - 2.0 * std::floor(s * 0.5);
    return 1.0 - std::abs(m - 1.0);
}
/**
 * Triangle wave smoothed to its first two Fourier terms: near-constant speed through the middle,
 * eased zero-velocity turnarounds. theta in radians, result in [-1, 1].
 */
inline double easedSweep(double theta)
{
    return 0.9 * (std::sin(theta) - std::sin(3.0 * theta) / 9.0);
}
inline uint32_t seedOf(int seed, uint32_t salt) { return uint32_t(seed) * 0x9E3779B1U + salt; }
inline double h01(int seed, uint32_t salt, uint32_t index)
{
    return fx::hash01(seedOf(seed, salt), index);
}

inline const LedLayout& layoutOf(const RenderContext& ctx)
{
    return ctx.layout ? *ctx.layout : LedLayout::cd2();
}

/**
 * Projects every LED onto the direction angleDeg (0 = right, 90 = down) into t, normalised to
 * [0, 1] across the layout.
 * @return The projected span in layout units.
 */
double projectLeds(const LedLayout& layout, double angleDeg, std::vector<double>& t);

/** Half the width between outer LED centres at height y, interpolated between rows; layout units. */
double halfWidthAt(const LedLayout& layout, double y);

/** Largest distance from the centroid to any LED, in layout units. */
double maxRadius(const LedLayout& layout);

/** A closed path through the layout, sampled evenly by arc length. */
struct LoopPath {
    double length = 0;              /**< layout units */
    std::vector<QPointF> pts;
    std::vector<double> ledS;       /**< per LED: arc position 0..1 of the nearest path point */
    std::vector<double> ledDist;    /**< per LED: distance to the path, layout units */
};
/** Smoothed loop through the outermost LEDs, clockwise from the top-left. */
LoopPath perimeterPath(const LedLayout& layout);
constexpr double kInnerPathInset = 3.2;
/** perimeterPath() shrunk towards the centroid by kInnerPathInset layout units. */
LoopPath innerPath(const LedLayout& layout);

inline Rgb white() { return {1.f, 1.f, 1.f}; }

/** Mixes towards white in OKLab; amount 0..1. */
inline Rgb whiten(const Rgb& c, double amount) { return color::mix(c, white(), float(amount)); }

/**
 * A gradient sampled as a loop. Gradients authored over 0..1 with distinct end colours are
 * re-spaced so the wrap gets a segment of its own instead of a hard jump.
 */
class CyclicPalette {
public:
    explicit CyclicPalette(const ColorGradient& g);
    /** @param hold 0..1 share of every segment that rests on its stop colour; 0 is a plain gradient. */
    Rgb sample(double u, double hold = 0.0) const;
    int size() const { return int(m_pos.size()); }

private:
    std::vector<double> m_pos;
    std::vector<Lab> m_lab;
};

/**
 * Accumulates lights given as colour + perceived level 0..1. Levels combine like screen,
 * 1 - prod(1 - level); the colour is their level-weighted OKLab mean, so overlaps never darken.
 */
struct LightAccum {
    double L = 0, A = 0, B = 0, w = 0, keep = 1;
    void add(const Rgb& c, double level)
    {
        if (!(level > 1e-5))
            return;
        level = std::min(level, 1.0);
        const Lab lab = color::toOklab(c);
        L += lab.L * level;
        A += lab.a * level;
        B += lab.b * level;
        w += level;
        keep *= 1.0 - level;
    }
    double level() const { return 1.0 - keep; }
    Rgb color() const
    {
        if (w <= 1e-9)
            return {};
        return color::fromOklab({float(L / w), float(A / w), float(B / w)});
    }
};

/**
 * One overlay pixel: c at perceived brightness level. Transparent mode returns straight alpha;
 * opaque mode composites over bg exactly as the scene's Normal blend would.
 */
inline Rgba overlayPixel(const Rgb& c, double level, bool transparent, const Rgb& bg)
{
    const float a = color::perceptualToLinear(float(clamp01(level)));
    if (transparent)
        return Rgba(c, a);
    return Rgba(bg.r + (c.r - bg.r) * a, bg.g + (c.g - bg.g) * a, bg.b + (c.b - bg.b) * a, 1.f);
}

/** 4-D value noise with quintic fade, lattice wrapped on y every periodY cells (0 = no wrap); roughly [0, 1]. */
double valueNoise4(double x, double y, double z, double w, uint32_t seed, int periodY);

/**
 * Seamless noise that scrolls along -y (features move up) by scroll whole y-periods per loop
 * while morphing along a time circle of radius morph.
 */
double scrollNoise(double x, double y, double phase, uint32_t seed, int periodY, int scroll,
                   double morph);

/** Two octaves of fx::loopNoise, seamless in phase; roughly [0, 1]. */
double loopFbm(double x, double y, double phase, uint32_t seed, double timeRadius = 1.0);

/** Stretches noise around 0.5 by contrast, clamped and softened to [0, 1]. */
inline double contrastStretch(double n, double contrast)
{
    const double v = 0.5 + (n - 0.5) * contrast;
    return smooth(0.0, 1.0, clamp01(v)) * 0.6 + clamp01(v) * 0.4;
}

} // namespace fxi
} // namespace fg
