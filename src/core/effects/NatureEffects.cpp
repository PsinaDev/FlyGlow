#include "EffectsInternal.h"

namespace fg {
namespace {

using namespace fxi;

const QString kCategory = QT_TRANSLATE_NOOP("Effects", "Nature");

class FireEffect : public Effect {
public:
    QString typeId() const override { return "fire"; }
    QString name() const override { return QT_TRANSLATE_NOOP("Effects", "Fire"); }
    QString category() const override { return kCategory; }
    QList<ParamDesc> params() const override
    {
        return {
            pGradient("gradient", QT_TRANSLATE_NOOP("Effects", "Palette"),
                      stopsPalette({{0.00, QColor(0, 0, 0)},
                                    {0.18, QColor(60, 0, 0)},
                                    {0.40, QColor(210, 22, 0)},
                                    {0.62, QColor(255, 95, 0)},
                                    {0.82, QColor(255, 170, 20)},
                                    {1.00, QColor(255, 235, 150)}})),
            pPercent("height", QT_TRANSLATE_NOOP("Effects", "Flame height"), 80),
            pPercent("turbulence", QT_TRANSLATE_NOOP("Effects", "Turbulence"), 60),
            pCycles(3),
            pSeed(1),
        };
    }
    void render(const RenderContext& ctx, Frame& out) const override
    {
        const LedLayout& L = layoutOf(ctx);
        const ColorGradient pal = g("gradient");
        const double height = std::max(f("height") / 100.0, 0.05);
        const double turb = f("turbulence") / 100.0;
        const int cycles = i("cycles");
        const uint32_t seed = seedOf(i("seed"), 0xF17E);
        const QRectF bb = L.bounds();
        constexpr int kPeriod = 5;   // noise cells per scroll period, first octave
        for (int k = 0; k < L.count(); ++k) {
            const QPointF p = L.led(k).pos;
            const double up = (bb.bottom() - p.y()) / bb.height();   // 0 at the tip, 1 at the top
            const double n1 = scrollNoise(p.x() * 0.42, p.y() * 0.40, ctx.phase, seed, kPeriod, cycles, 0.45);
            const double n2 = scrollNoise(p.x() * 0.84 + 9.1, p.y() * 0.80, ctx.phase, seed + 101U,
                                          2 * kPeriod, cycles, 0.6);
            const double n = n1 * 0.7 + n2 * 0.3;
            const double hw = std::max(halfWidthAt(L, p.y()), 0.5);
            const double side = std::abs(p.x()) / (hw + 0.5);
            double heat = 1.0 - up / height;
            heat += (n - 0.5) * turb * (0.7 + 1.3 * up);
            heat *= 0.8 + 0.45 * n;
            heat -= 0.15 * side * side;
            heat = clamp01(heat * 1.08);
            out[size_t(k)] = Rgba(pal.sample(heat), 1.f);
        }
    }
};

class PlasmaEffect : public Effect {
public:
    QString typeId() const override { return "plasma"; }
    QString name() const override { return QT_TRANSLATE_NOOP("Effects", "Plasma"); }
    QString category() const override { return kCategory; }
    QList<ParamDesc> params() const override
    {
        return {
            pGradient("gradient", QT_TRANSLATE_NOOP("Effects", "Colors"),
                      linearPalette({QColor(0, 60, 255), QColor(140, 0, 255), QColor(255, 0, 130),
                                     QColor(255, 120, 20)})),
            pFloat("scale", QT_TRANSLATE_NOOP("Effects", "Scale"), 1.0, 0.3, 3.0, 0.05, "×"),
            pInt("colorCycle", QT_TRANSLATE_NOOP("Effects", "Palette cycles"), 1, 0, 4, "×"),
            pCycles(1),
        };
    }
    void render(const RenderContext& ctx, Frame& out) const override
    {
        const LedLayout& L = layoutOf(ctx);
        const ColorGradient pal = g("gradient");
        const double scale = f("scale") * 0.5;
        const int flow = i("colorCycle");
        const int cycles = i("cycles");
        const double T = kTau * ctx.phase * cycles;
        const double F = kTau * ctx.phase * flow;
        const QPointF centre = L.centroid();
        const double cx = 2.6 * std::cos(T), cy = 2.0 * std::sin(T);
        for (int k = 0; k < L.count(); ++k) {
            const double X = (L.led(k).pos.x() - centre.x()) * scale;
            const double Y = (L.led(k).pos.y() - centre.y()) * scale;
            double v = std::sin(X * 1.1 + T);
            v += std::sin(Y * 1.3 - T + 1.0);
            v += std::sin((X * 0.7 + Y * 0.7) + T + 2.0);
            v += std::sin(std::hypot(X - cx, Y - cy) * 1.4 - T);
            // A rounded triangle fold maps v in [-4, 4] onto the palette end to end, with no wrap segment.
            const double u = 0.5 + 0.5 * std::asin(0.97 * std::sin(v * 0.95 + F)) / std::asin(0.97);
            out[size_t(k)] = Rgba(pal.sample(u), 1.f);
        }
    }
};

class AuroraEffect : public Effect {
public:
    QString typeId() const override { return "aurora"; }
    QString name() const override { return QT_TRANSLATE_NOOP("Effects", "Aurora"); }
    QString category() const override { return kCategory; }
    QList<ParamDesc> params() const override
    {
        return {
            pGradient("gradient", QT_TRANSLATE_NOOP("Effects", "Colors (bottom → top)"),
                      stopsPalette({{0.0, QColor(40, 255, 140)},
                                    {0.35, QColor(0, 230, 200)},
                                    {0.7, QColor(60, 110, 255)},
                                    {1.0, QColor(170, 60, 255)}})),
            pColor("background", QT_TRANSLATE_NOOP("Effects", "Background"), QColor(0, 6, 24)),
            pPercent("intensity", QT_TRANSLATE_NOOP("Effects", "Brightness"), 100),
            pCycles(1),
            pSeed(3),
        };
    }
    void render(const RenderContext& ctx, Frame& out) const override
    {
        const LedLayout& L = layoutOf(ctx);
        const ColorGradient pal = g("gradient");
        const Rgb bg = c("background");
        const double intensity = f("intensity") / 100.0;
        const int cycles = i("cycles");
        const int seed = i("seed");
        const double ph = fract(ctx.phase * cycles);
        const double T = kTau * ph;
        struct Curtain {
            double base, len, amp, k1, k2, strength;
            int dir;
        };
        const Curtain curtains[] = {
            {-2.6, 4.2, 1.0, 0.42, 0.95, 1.00, 1},
            {0.9, 3.6, 1.2, 0.34, 0.80, 0.90, -1},
            {3.7, 2.8, 0.8, 0.50, 1.10, 0.75, 1},
        };
        for (int k = 0; k < L.count(); ++k) {
            const QPointF p = L.led(k).pos;
            // Averaging palette positions rather than colours keeps overlaps on the palette instead of grey.
            double keep = 1.0, wsum = 0.0, tsum = 0.0;
            auto add = [&](double t, double level) {
                level = clamp01(level);
                keep *= 1.0 - level;
                wsum += level;
                tsum += t * level;
            };
            int j = 0;
            for (const Curtain& cu : curtains) {
                const uint32_t s = seedOf(seed, 0xA0 + uint32_t(j));
                const double ph1 = h01(seed, 1, uint32_t(j)), ph2 = h01(seed, 2, uint32_t(j));
                const double wob = fx::loopNoise(p.x() * 0.15 + j * 7.0, j * 3.3, ph, s, 0.6) - 0.5;
                const double edge = cu.base + cu.amp * std::sin(cu.k1 * p.x() + cu.dir * T + kTau * ph1)
                                    + 0.4 * cu.amp * std::sin(cu.k2 * p.x() - cu.dir * 2.0 * T + kTau * ph2)
                                    + 1.8 * wob;
                const double d = edge - p.y();   // > 0: above the lower edge
                const double rim = gauss(d - 0.3, 0.6);
                const double glow = smooth(-0.6, 0.5, d) * std::exp(-std::max(d, 0.0) / cu.len);
                const double rays = fx::loopNoise(p.x() * 0.55 + j * 13.0, p.y() * 0.05 + j * 2.0, ph, s + 17U, 0.9);
                const double shimmer = 0.62 + 0.38 * smooth(0.2, 0.8, rays);
                // Rim and glow are separate lights so the glow keeps its cooler colour under the green rim.
                add(0.05, rim * shimmer * cu.strength * intensity);
                add(clamp01(0.15 + std::max(d, 0.0) / cu.len), 0.95 * glow * shimmer * cu.strength * intensity);
                ++j;
            }
            const Rgb col = pal.sample(wsum > 1e-9 ? tsum / wsum : 0.0);
            out[size_t(k)] = overlayPixel(col, 1.0 - keep, false, bg);
        }
    }
};

class LavaLampEffect : public Effect {
public:
    QString typeId() const override { return "lava_lamp"; }
    QString name() const override { return QT_TRANSLATE_NOOP("Effects", "Lava lamp"); }
    QString category() const override { return kCategory; }
    QList<ParamDesc> params() const override
    {
        return {
            pGradient("gradient", QT_TRANSLATE_NOOP("Effects", "Blob colors"),
                      linearPalette({QColor(255, 60, 0), QColor(255, 0, 120), QColor(255, 160, 0)})),
            pColor("background", QT_TRANSLATE_NOOP("Effects", "Background"), QColor(30, 0, 52)),
            pInt("count", QT_TRANSLATE_NOOP("Effects", "Blobs"), 5, 2, 8),
            pFloat("size", QT_TRANSLATE_NOOP("Effects", "Size"), 1.0, 0.5, 2.0, 0.05, "×"),
            pPercent("softness", QT_TRANSLATE_NOOP("Effects", "Edge softness"), 50),
            pCycles(1),
            pSeed(2),
        };
    }
    void render(const RenderContext& ctx, Frame& out) const override
    {
        const LedLayout& L = layoutOf(ctx);
        const ColorGradient pal = g("gradient");
        const Rgb bg = c("background");
        const int count = i("count");
        const double size = f("size");
        const double soft = f("softness") / 100.0;
        const int cycles = i("cycles");
        const int seed = i("seed");
        const QRectF bb = L.bounds();

        struct Blob {
            QPointF c;
            double r2;
            Lab lab;
        };
        std::vector<Blob> blobs;
        for (int b = 0; b < count; ++b) {
            const uint32_t id = uint32_t(b);
            const int fy = cycles * (h01(seed, 11, id) < 0.35 ? 2 : 1);
            const int fxq = cycles * (h01(seed, 12, id) < 0.5 ? 2 : 1);
            const double ty = 0.5 + 0.5 * std::sin(kTau * (fy * ctx.phase + h01(seed, 13, id)));
            const double y = lerp(bb.top() + 1.2, bb.bottom() - 2.0, ty);
            const double ux = (h01(seed, 14, id) * 2.0 - 1.0) * 0.55
                              + 0.3 * std::sin(kTau * (fxq * ctx.phase + h01(seed, 15, id)));
            const double x = ux * halfWidthAt(L, y) * 0.85;
            const double r = size * (1.0 + 0.6 * h01(seed, 16, id));
            blobs.push_back({QPointF(x, y), r * r,
                             color::toOklab(pal.sample(h01(seed, 17, id)))});
        }
        const double edge = lerp(0.08, 0.45, soft);
        for (int k = 0; k < L.count(); ++k) {
            const QPointF p = L.led(k).pos;
            double F = 0, w = 0, la = 0, aa = 0, ba = 0;
            for (const Blob& bl : blobs) {
                const QPointF d = p - bl.c;
                const double v = bl.r2 / (d.x() * d.x() + d.y() * d.y() + 0.35);
                F += v;
                // v^6 weights: each region takes its nearest blob's colour and blends only where blobs meet.
                const double wv = (v * v) * (v * v) * (v * v);
                w += wv;
                la += bl.lab.L * wv;
                aa += bl.lab.a * wv;
                ba += bl.lab.b * wv;
            }
            const Rgb blobCol = color::fromOklab({float(la / w), float(aa / w), float(ba / w)});
            const double m = smooth(1.0 - edge, 1.0 + edge, F);
            const double lit = lerp(0.70, 1.0, smooth(1.0, 3.5, F));
            out[size_t(k)] = Rgba(color::mix(bg, color::dim(blobCol, float(lit)), float(m)), 1.f);
        }
    }
};

class NoiseFlowEffect : public Effect {
public:
    QString typeId() const override { return "noise_flow"; }
    QString name() const override { return QT_TRANSLATE_NOOP("Effects", "Noise flow"); }
    QString category() const override { return kCategory; }
    QList<ParamDesc> params() const override
    {
        return {
            pGradient("gradient", QT_TRANSLATE_NOOP("Effects", "Colors"),
                      stopsPalette({{0.00, QColor(0, 6, 50)},
                                    {0.35, QColor(0, 40, 210)},
                                    {0.60, QColor(0, 120, 255)},
                                    {0.85, QColor(0, 220, 230)},
                                    {1.00, QColor(120, 255, 215)}})),
            pFloat("scale", QT_TRANSLATE_NOOP("Effects", "Scale"), 1.0, 0.3, 3.0, 0.05, "×"),
            pPercent("contrast", QT_TRANSLATE_NOOP("Effects", "Contrast"), 40),
            pCycles(1),
            pSeed(4),
        };
    }
    void render(const RenderContext& ctx, Frame& out) const override
    {
        const LedLayout& L = layoutOf(ctx);
        const ColorGradient pal = g("gradient");
        const double scale = f("scale") * 0.26;
        const double contrast = lerp(1.0, 2.8, f("contrast") / 100.0);
        const double ph = fract(ctx.phase * i("cycles"));
        const uint32_t seed = seedOf(i("seed"), 0x9015E);
        for (int k = 0; k < L.count(); ++k) {
            const QPointF p = L.led(k).pos;
            const double X = p.x() * scale, Y = p.y() * scale;
            // A spatial lag on the time circle makes lattice crossings sweep across the dock, not flip all LEDs at once.
            const double n = loopFbm(X, Y, fract(ph + 0.09 * (X + 0.6 * Y) + 1.0), seed, 0.9);
            out[size_t(k)] = Rgba(pal.sample(contrastStretch(n, contrast)), 1.f);
        }
    }
};

} // namespace

void registerNatureEffects(EffectRegistry& r)
{
    r.add<FireEffect>();
    r.add<PlasmaEffect>();
    r.add<AuroraEffect>();
    r.add<LavaLampEffect>();
    r.add<NoiseFlowEffect>();
}

} // namespace fg
