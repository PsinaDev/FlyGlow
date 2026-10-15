#include "EffectsInternal.h"

namespace fg {
namespace {

using namespace fxi;

const QString kCategory = QT_TRANSLATE_NOOP("Effects", "Waves");

Rgb pastel(const Rgb& c, double saturation)
{
    return saturation >= 0.999 ? c : whiten(c, 1.0 - clamp01(saturation));
}

QStringList rotationChoices()
{
    return {QT_TRANSLATE_NOOP("Effects", "Clockwise"), QT_TRANSLATE_NOOP("Effects", "Counterclockwise")};
}

class RainbowWaveEffect : public Effect {
public:
    QString typeId() const override { return "rainbow_wave"; }
    QString name() const override { return QT_TRANSLATE_NOOP("Effects", "Rainbow wave"); }
    QString category() const override { return kCategory; }
    QList<ParamDesc> params() const override
    {
        return {
            pAngle(0),
            pFloat("scale", QT_TRANSLATE_NOOP("Effects", "Waves across the dock"), 1.0, 0.25, 4.0, 0.05, "×"),
            pPercent("saturation", QT_TRANSLATE_NOOP("Effects", "Saturation"), 100),
            pCycles(1),
        };
    }
    void render(const RenderContext& ctx, Frame& out) const override
    {
        const LedLayout& L = layoutOf(ctx);
        std::vector<double> t;
        projectLeds(L, f("angle"), t);
        const double scale = f("scale");
        const double sat = f("saturation") / 100.0;
        const double shift = ctx.phase * i("cycles");
        for (int k = 0; k < L.count(); ++k) {
            const Rgb col = color::rainbow(float(fract(t[size_t(k)] * scale - shift)));
            out[size_t(k)] = Rgba(pastel(col, sat), 1.f);
        }
    }
};

class SpiralRainbowEffect : public Effect {
public:
    QString typeId() const override { return "spiral_rainbow"; }
    QString name() const override { return QT_TRANSLATE_NOOP("Effects", "Rainbow spiral"); }
    QString category() const override { return kCategory; }
    QList<ParamDesc> params() const override
    {
        return {
            pInt("arms", QT_TRANSLATE_NOOP("Effects", "Arms"), 1, 1, 6),
            pFloat("twist", QT_TRANSLATE_NOOP("Effects", "Twist"), 0.7, -3.0, 3.0, 0.05),
            pChoice("direction", QT_TRANSLATE_NOOP("Effects", "Rotation"), rotationChoices(), 0),
            pPercent("saturation", QT_TRANSLATE_NOOP("Effects", "Saturation"), 100),
            pCycles(1),
        };
    }
    void render(const RenderContext& ctx, Frame& out) const override
    {
        const LedLayout& L = layoutOf(ctx);
        const int arms = i("arms");
        const double twist = f("twist");
        const double dir = i("direction") == 0 ? 1.0 : -1.0;
        const double sat = f("saturation") / 100.0;
        const double shift = dir * ctx.phase * i("cycles");
        for (int k = 0; k < L.count(); ++k) {
            const Led& led = L.led(k);
            const double h = arms * led.angle / kTau + twist * led.radius - shift;
            out[size_t(k)] = Rgba(pastel(color::rainbow(float(fract(h))), sat), 1.f);
        }
    }
};

class ColorWaveEffect : public Effect {
public:
    QString typeId() const override { return "color_wave"; }
    QString name() const override { return QT_TRANSLATE_NOOP("Effects", "Color wave"); }
    QString category() const override { return kCategory; }
    QList<ParamDesc> params() const override
    {
        return {
            pGradient("gradient", QT_TRANSLATE_NOOP("Effects", "Colors"),
                      cyclicPalette({QColor(0, 70, 255), QColor(0, 220, 255), QColor(160, 0, 255)})),
            pAngle(0),
            pPercent("width", QT_TRANSLATE_NOOP("Effects", "Band width"), 40),
            pPercent("softness", QT_TRANSLATE_NOOP("Effects", "Transition softness"), 70),
            pCycles(1),
        };
    }
    void render(const RenderContext& ctx, Frame& out) const override
    {
        const LedLayout& L = layoutOf(ctx);
        std::vector<double> t;
        const double span = projectLeds(L, f("angle"), t);
        const CyclicPalette pal(g("gradient"));
        const int n = std::max(1, pal.size());
        const double width = std::max(f("width") / 100.0, 0.03);
        // Never sharper than ~1.2 LED pitches, so moving edges stay anti-aliased.
        const double minSoft = 1.2 / (width * span);
        const double soft = clamp01(std::max(f("softness") / 100.0, minSoft));
        const double period = n * width;
        const double shift = ctx.phase * i("cycles");
        for (int k = 0; k < L.count(); ++k)
            out[size_t(k)] = Rgba(pal.sample(t[size_t(k)] / period - shift, 1.0 - soft), 1.f);
    }
};

class RippleEffect : public Effect {
public:
    QString typeId() const override { return "ripple"; }
    QString name() const override { return QT_TRANSLATE_NOOP("Effects", "Ripples"); }
    QString category() const override { return kCategory; }
    QList<ParamDesc> params() const override
    {
        QList<ParamDesc> p{
            pGradient("gradient", QT_TRANSLATE_NOOP("Effects", "Colors"),
                      cyclicPalette({QColor(0, 190, 255), QColor(120, 50, 255), QColor(0, 255, 190)})),
            pInt("count", QT_TRANSLATE_NOOP("Effects", "Rings at once"), 3, 1, 6),
            pFloat("width", QT_TRANSLATE_NOOP("Effects", "Ring width"), 1.8, 0.5, 5.0, 0.1),
            pPercent("fade", QT_TRANSLATE_NOOP("Effects", "Fade toward the edge"), 55),
            pCycles(1),
        };
        addOverlayParams(p, QColor(0, 4, 18));
        return p;
    }
    void render(const RenderContext& ctx, Frame& out) const override
    {
        const LedLayout& L = layoutOf(ctx);
        const CyclicPalette pal(g("gradient"));
        const int count = i("count");
        const int cycles = i("cycles");
        const double sigma = f("width") * 0.42;   // FWHM ~ width
        const double fade = f("fade") / 100.0;
        const bool transparent = b("transparent");
        const Rgb bg = c("background");
        const double rMax = maxRadius(L);
        const double travel = rMax + 3.0 * sigma;
        const double spacing = travel / count;
        const long long M = (long long)count * cycles;
        const QPointF centre = L.centroid();
        const double reach = 3.2 * sigma / spacing;
        for (int k = 0; k < L.count(); ++k) {
            const QPointF p = L.led(k).pos;
            const double d = std::hypot(p.x() - centre.x(), p.y() - centre.y());
            const double x = ctx.phase * double(M) - d / spacing;
            LightAccum acc;
            for (long long n = (long long)std::ceil(x - reach); n <= (long long)std::floor(x + reach); ++n) {
                const double r = (ctx.phase * double(M) - double(n)) * spacing;
                const double prof = gauss(d - r, sigma);
                const double age = clamp01(r / rMax);
                const double life = smooth(-1.5 * sigma, 0.6 * sigma, r) * (1.0 - fade * age);
                // With a ring count that is a multiple of the stop count, every ring is a pure stop colour.
                const Rgb col = pal.sample(double(lmod(n, M)) / double(M), 0.9);
                acc.add(col, prof * life);
            }
            out[size_t(k)] = overlayPixel(acc.color(), acc.level(), transparent, bg);
        }
    }
};

class VisorEffect : public Effect {
public:
    QString typeId() const override { return "visor"; }
    QString name() const override { return QT_TRANSLATE_NOOP("Effects", "Visor"); }
    QString category() const override { return kCategory; }
    QList<ParamDesc> params() const override
    {
        QList<ParamDesc> p{
            pColor("color", QT_TRANSLATE_NOOP("Effects", "Color"), QColor(255, 28, 40)),
            pColor("trailColor", QT_TRANSLATE_NOOP("Effects", "Trail color"), QColor(150, 0, 80)),
            pAngle(0),
            pFloat("width", QT_TRANSLATE_NOOP("Effects", "Width"), 2.2, 0.6, 6.0, 0.1),
            pPercent("trail", QT_TRANSLATE_NOOP("Effects", "Trail"), 60),
            pCycles(1),
        };
        addOverlayParams(p, QColor(10, 0, 4));
        return p;
    }
    void render(const RenderContext& ctx, Frame& out) const override
    {
        const LedLayout& L = layoutOf(ctx);
        const double a = f("angle") * kPi / 180.0;
        const double dx = std::cos(a), dy = std::sin(a);
        double lo = 1e9, hi = -1e9;
        std::vector<double> p(size_t(L.count()));
        for (int k = 0; k < L.count(); ++k) {
            p[size_t(k)] = L.led(k).pos.x() * dx + L.led(k).pos.y() * dy;
            lo = std::min(lo, p[size_t(k)]);
            hi = std::max(hi, p[size_t(k)]);
        }
        const int cycles = i("cycles");
        const double sigma = f("width") * 0.42;
        // Overshoot the edge LEDs slightly so the head fully lights the last row.
        const double margin = 0.3;
        auto headAt = [&](double phase) {
            const double s = easedSweep(kTau * phase * cycles - kPi / 2.0);   // starts at lo
            return lerp(lo - margin, hi + margin, 0.5 + 0.5 * s);
        };
        // Trail length in loop phase, up to one full sweep.
        const double trailT = std::max(f("trail") / 100.0, 0.001) * 0.5 / cycles;
        constexpr int kSamples = 64;
        double heads[kSamples + 1];
        double decay[kSamples + 1];
        for (int s = 0; s <= kSamples; ++s) {
            const double age = trailT * s / kSamples;
            heads[s] = headAt(ctx.phase - age);
            const double u = double(s) / kSamples;
            decay[s] = s == 0 ? 1.0 : std::pow(1.0 - u, 1.6);
        }
        const Rgb head = c("color"), trail = c("trailColor");
        const bool transparent = b("transparent");
        const Rgb bg = c("background");
        for (int k = 0; k < L.count(); ++k) {
            double best = 0, wsum = 0, agesum = 0;
            for (int s = 0; s <= kSamples; ++s) {
                const double v = gauss(p[size_t(k)] - heads[s], sigma) * decay[s];
                best = std::max(best, v);
                const double w = v * v * v * v;
                wsum += w;
                agesum += w * double(s) / kSamples;
            }
            const double age = wsum > 1e-12 ? agesum / wsum : 1.0;
            const Rgb col = color::mix(head, trail, float(smooth(0.0, 0.8, age)));
            out[size_t(k)] = overlayPixel(col, best, transparent, bg);
        }
    }
};

class RadarEffect : public Effect {
public:
    QString typeId() const override { return "radar"; }
    QString name() const override { return QT_TRANSLATE_NOOP("Effects", "Radar"); }
    QString category() const override { return kCategory; }
    QList<ParamDesc> params() const override
    {
        QList<ParamDesc> p{
            pColor("color", QT_TRANSLATE_NOOP("Effects", "Color"), QColor(0, 255, 150)),
            pColor("trailColor", QT_TRANSLATE_NOOP("Effects", "Trail color"), QColor(0, 120, 255)),
            pPercent("trail", QT_TRANSLATE_NOOP("Effects", "Trail"), 55),
            pFloat("width", QT_TRANSLATE_NOOP("Effects", "Beam width"), 1.4, 0.3, 3.0, 0.05),
            pChoice("direction", QT_TRANSLATE_NOOP("Effects", "Rotation"), rotationChoices(), 0),
            pCycles(1),
        };
        addOverlayParams(p, QColor(0, 8, 10));
        return p;
    }
    void render(const RenderContext& ctx, Frame& out) const override
    {
        const LedLayout& L = layoutOf(ctx);
        const double dir = i("direction") == 0 ? 1.0 : -1.0;
        const double beam = -kPi / 2.0 + dir * kTau * ctx.phase * i("cycles");   // starts pointing up
        const double trailLen = std::max(f("trail") / 100.0, 0.01) * 0.9;           // of a turn
        const double sigma = f("width") * 0.42;
        const Rgb head = c("color"), trail = c("trailColor");
        const bool transparent = b("transparent");
        const Rgb bg = c("background");
        const QPointF centre = L.centroid();
        const double bx = std::cos(beam), by = std::sin(beam);
        for (int k = 0; k < L.count(); ++k) {
            const QPointF v = L.led(k).pos - centre;
            const double r = std::hypot(v.x(), v.y());
            const double along = v.x() * bx + v.y() * by;
            const double perp = std::abs(v.x() * by - v.y() * bx);
            const double beamV = gauss(perp, sigma) * smooth(-0.8, 0.4, along);
            const double ang = std::atan2(v.y(), v.x());
            const double behind = fract(dir * (beam - ang) / kTau);
            const double tu = behind / trailLen;
            const double trailV = tu < 1.0 ? std::pow(1.0 - tu, 1.8) * smooth(0.0, 1.2, r) : 0.0;
            const double level = std::max(beamV, 0.7 * trailV);
            const double hw = beamV / std::max(level, 1e-9);
            const Rgb col = color::mix(color::mix(head, trail, float(smooth(0.0, 0.55, tu))),
                                       whiten(head, 0.25), float(hw * hw));
            out[size_t(k)] = overlayPixel(col, level, transparent, bg);
        }
    }
};

} // namespace

void registerWaveEffects(EffectRegistry& r)
{
    r.add<RainbowWaveEffect>();
    r.add<SpiralRainbowEffect>();
    r.add<ColorWaveEffect>();
    r.add<RippleEffect>();
    r.add<VisorEffect>();
    r.add<RadarEffect>();
}

} // namespace fg
