#include "EffectsInternal.h"

namespace fg {
namespace {

using namespace fxi;

const QString kCategory = QT_TRANSLATE_NOOP("Effects", "Basic");

class StaticEffect : public Effect {
public:
    QString typeId() const override { return "static"; }
    QString name() const override { return QT_TRANSLATE_NOOP("Effects", "Static color"); }
    QString category() const override { return kCategory; }
    QList<ParamDesc> params() const override
    {
        return {pColor("color", QT_TRANSLATE_NOOP("Effects", "Color"), QColor(0, 116, 255))};
    }
    bool isAnimated() const override { return false; }
    void render(const RenderContext&, Frame& out) const override
    {
        const Rgb col = c("color");
        for (Rgba& px : out)
            px = Rgba(col, 1.f);
    }
};

class GradientEffect : public Effect {
public:
    QString typeId() const override { return "gradient"; }
    QString name() const override { return QT_TRANSLATE_NOOP("Effects", "Gradient"); }
    QString category() const override { return kCategory; }
    QList<ParamDesc> params() const override
    {
        return {
            pGradient("gradient", QT_TRANSLATE_NOOP("Effects", "Gradient"),
                      linearPalette({QColor(0, 110, 255), QColor(150, 40, 255),
                                     QColor(255, 40, 140)})),
            pAngle(90),
            pFloat("scale", QT_TRANSLATE_NOOP("Effects", "Scale"), 1.0, 0.25, 4.0, 0.05, "×"),
            pChoice("mode", QT_TRANSLATE_NOOP("Effects", "Repeat"),
                    {QT_TRANSLATE_NOOP("Effects", "Mirror"), QT_TRANSLATE_NOOP("Effects", "Wrap around")}, 0),
            pCycles(0, 0),
        };
    }
    bool isAnimated() const override { return i("cycles") > 0; }
    void render(const RenderContext& ctx, Frame& out) const override
    {
        const LedLayout& L = layoutOf(ctx);
        std::vector<double> t;
        projectLeds(L, f("angle"), t);
        const ColorGradient grad = g("gradient");
        const double scale = f("scale");
        const int cycles = i("cycles");
        const bool mirror = i("mode") == 0;
        if (mirror) {
            // tri() has period 2, so the shift moves 2 per cycle to stay seamless.
            const double shift = 2.0 * cycles * ctx.phase;
            for (int k = 0; k < L.count(); ++k)
                out[size_t(k)] = Rgba(grad.sample(tri(t[size_t(k)] * scale - shift)), 1.f);
        } else {
            const CyclicPalette pal(grad);
            const double shift = cycles * ctx.phase;
            for (int k = 0; k < L.count(); ++k)
                out[size_t(k)] = Rgba(pal.sample(t[size_t(k)] * scale - shift), 1.f);
        }
    }
};

class ColorShiftEffect : public Effect {
public:
    QString typeId() const override { return "color_shift"; }
    QString name() const override { return QT_TRANSLATE_NOOP("Effects", "Color shift"); }
    QString category() const override { return kCategory; }
    QList<ParamDesc> params() const override
    {
        return {
            pGradient("gradient", QT_TRANSLATE_NOOP("Effects", "Colors"), ColorGradient::rainbow()),
            pPercent("hold", QT_TRANSLATE_NOOP("Effects", "Hold on each color"), 0),
            pPercent("spread", QT_TRANSLATE_NOOP("Effects", "Spread across the dock"), 12),
            pAngle(90),
            pCycles(1),
        };
    }
    void render(const RenderContext& ctx, Frame& out) const override
    {
        const LedLayout& L = layoutOf(ctx);
        std::vector<double> t;
        projectLeds(L, f("angle"), t);
        const CyclicPalette pal(g("gradient"));
        const double hold = f("hold") / 100.0;
        const double spread = f("spread") / 100.0;
        const double u0 = ctx.phase * i("cycles");
        for (int k = 0; k < L.count(); ++k)
            out[size_t(k)] = Rgba(pal.sample(u0 - spread * t[size_t(k)], hold), 1.f);
    }
};

class BreathingEffect : public Effect {
public:
    QString typeId() const override { return "breathing"; }
    QString name() const override { return QT_TRANSLATE_NOOP("Effects", "Breathing"); }
    QString category() const override { return kCategory; }
    QList<ParamDesc> params() const override
    {
        return {
            pGradient("gradient", QT_TRANSLATE_NOOP("Effects", "Colors"),
                      cyclicPalette({QColor(0, 120, 255), QColor(200, 40, 255)})),
            pPercent("min", QT_TRANSLATE_NOOP("Effects", "Minimum brightness"), 0),
            pPercent("softness", QT_TRANSLATE_NOOP("Effects", "Softness"), 60),
            pPercent("spread", QT_TRANSLATE_NOOP("Effects", "Wave from the center"), 25),
            pCycles(2),
        };
    }
    void render(const RenderContext& ctx, Frame& out) const override
    {
        const LedLayout& L = layoutOf(ctx);
        const CyclicPalette pal(g("gradient"));
        const int cycles = i("cycles");
        const double minL = f("min") / 100.0;
        const double soft = f("softness") / 100.0;
        const double spread = f("spread") / 100.0;
        // Softness reshapes the raised cosine from a short pulse with a long dark rest to a long glow.
        const double gamma = lerp(2.2, 0.7, soft);
        for (int k = 0; k < L.count(); ++k) {
            // The centre leads slightly, so the breath swells outwards.
            const double x = ctx.phase * cycles - spread * 0.14 * L.led(k).radius;
            const double s = 0.5 - 0.5 * std::cos(kTau * x);
            const double level = minL + (1.0 - minL) * std::pow(s, gamma);
            // The colour changes once per breath, eased around the trough, so a high minimum never jumps.
            const double y = x + 0.5;
            const double fl = std::floor(y);
            const double ci = fl - 1.0 + smooth(0.22, 0.78, y - fl);
            const Rgb col = pal.sample(ci / cycles);
            out[size_t(k)] = Rgba(color::dim(col, float(level)), 1.f);
        }
    }
};

// (u e^(1-u))^3: zero with zero slope at dt = 0, peak 1 at dt = tau, then a long soft decay.
inline double beatPulse(double dt, double tau)
{
    const double u = dt / tau;
    const double v = u * std::exp(1.0 - u);
    return v * v * v;
}

class HeartbeatEffect : public Effect {
public:
    QString typeId() const override { return "heartbeat"; }
    QString name() const override { return QT_TRANSLATE_NOOP("Effects", "Heartbeat"); }
    QString category() const override { return kCategory; }
    QList<ParamDesc> params() const override
    {
        return {
            pColor("color", QT_TRANSLATE_NOOP("Effects", "Color"), QColor(255, 16, 56)),
            pPercent("base", QT_TRANSLATE_NOOP("Effects", "Base glow"), 18),
            pPercent("second", QT_TRANSLATE_NOOP("Effects", "Second beat"), 65),
            pPercent("spread", QT_TRANSLATE_NOOP("Effects", "Wave from the center"), 45),
            pCycles(3),
        };
    }
    void render(const RenderContext& ctx, Frame& out) const override
    {
        const LedLayout& L = layoutOf(ctx);
        const Rgb col = c("color");
        const double base = f("base") / 100.0;
        const double second = f("second") / 100.0;
        const double spread = f("spread") / 100.0;
        const int cycles = i("cycles");
        for (int k = 0; k < L.count(); ++k) {
            const double r = L.led(k).radius;
            const double x = fract(ctx.phase * cycles - spread * 0.10 * r);
            const double lub = beatPulse(x, 0.10);
            const double dub = beatPulse(fract(x - 0.24), 0.10);
            // Soft tanh shoulder instead of a hard clip where the beats overlap.
            const double sum = lub + second * dub;
            double beat = sum < 0.8 ? sum : 0.8 + 0.2 * std::tanh((sum - 0.8) / 0.2);
            beat *= 1.0 - 0.35 * spread * r;
            const double level = base + (1.0 - base) * beat;
            out[size_t(k)] = Rgba(color::dim(col, float(level)), 1.f);
        }
    }
};

} // namespace

void registerBasicEffects(EffectRegistry& r)
{
    r.add<StaticEffect>();
    r.add<GradientEffect>();
    r.add<ColorShiftEffect>();
    r.add<BreathingEffect>();
    r.add<HeartbeatEffect>();
}

} // namespace fg
