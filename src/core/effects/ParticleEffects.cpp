#include "EffectsInternal.h"

namespace fg {
namespace {

using namespace fxi;

const QString kCategory = QT_TRANSLATE_NOOP("Effects", "Particles");

class StarsEffect : public Effect {
public:
    QString typeId() const override { return "stars"; }
    QString name() const override { return QT_TRANSLATE_NOOP("Effects", "Stars"); }
    QString category() const override { return kCategory; }
    QList<ParamDesc> params() const override
    {
        QList<ParamDesc> p{
            pGradient("gradient", QT_TRANSLATE_NOOP("Effects", "Colors"),
                      linearPalette({QColor(255, 255, 255), QColor(150, 190, 255), QColor(255, 210, 150)})),
            pPercent("density", QT_TRANSLATE_NOOP("Effects", "Density"), 45),
            pCycles(2),
            pSeed(1),
        };
        addOverlayParams(p, QColor(0, 3, 16));
        return p;
    }
    void render(const RenderContext& ctx, Frame& out) const override
    {
        const LedLayout& L = layoutOf(ctx);
        const ColorGradient pal = g("gradient");
        const double density = f("density") / 100.0;
        const int cycles = i("cycles");
        const int seed = i("seed");
        const bool transparent = b("transparent");
        const Rgb bg = c("background");
        for (int k = 0; k < L.count(); ++k) {
            const uint32_t led = uint32_t(k);
            // Twinkle rates are whole numbers per loop so the pattern repeats seamlessly.
            const int rate = cycles * (h01(seed, 1, led) < 0.4 ? 2 : 1);
            const double x = ctx.phase * rate + h01(seed, 2, led);
            const double m = std::floor(x);
            const double fr = x - m;
            const uint32_t id = led * 64U + uint32_t(lmod((long long)m, rate));
            double level = 0;
            Rgb col;
            if (h01(seed, 3, id) < density) {
                const double dur = 0.55 + 0.4 * h01(seed, 4, id);
                if (fr < dur) {
                    const double s = std::sin(kPi * fr / dur);
                    level = s * s * (0.55 + 0.45 * h01(seed, 5, id));
                }
                col = pal.sample(h01(seed, 6, id));
            }
            out[size_t(k)] = overlayPixel(col, level, transparent, bg);
        }
    }
};

class RainEffect : public Effect {
public:
    QString typeId() const override { return "rain"; }
    QString name() const override { return QT_TRANSLATE_NOOP("Effects", "Rain"); }
    QString category() const override { return kCategory; }
    QList<ParamDesc> params() const override
    {
        QList<ParamDesc> p{
            pGradient("gradient", QT_TRANSLATE_NOOP("Effects", "Colors"),
                      linearPalette({QColor(0, 110, 255), QColor(70, 210, 255)})),
            pPercent("density", QT_TRANSLATE_NOOP("Effects", "Density"), 45),
            pFloat("length", QT_TRANSLATE_NOOP("Effects", "Trail length"), 4.0, 0.5, 10.0, 0.1),
            pCycles(2),
            pSeed(1),
        };
        addOverlayParams(p, QColor(0, 3, 14));
        return p;
    }
    void render(const RenderContext& ctx, Frame& out) const override
    {
        const LedLayout& L = layoutOf(ctx);
        const ColorGradient pal = g("gradient");
        const int drops = 6 + int(std::lround(f("density") / 100.0 * 34.0));
        const double len = std::max(f("length"), 0.3);
        const int cycles = i("cycles");
        const int seed = i("seed");
        const bool transparent = b("transparent");
        const Rgb bg = c("background");
        const QRectF bb = L.bounds();
        constexpr double kHeadSigma = 0.55;
        // A head travels from fully above the dock until its trail has left the bottom.
        const double y0 = bb.top() - 3.2 * kHeadSigma;
        const double y1 = bb.bottom() + 3.2 * kHeadSigma + len;
        std::vector<LightAccum> acc(size_t(L.count()));
        for (int j = 0; j < drops; ++j) {
            const uint32_t id = uint32_t(j);
            // Stratified lanes and golden-ratio start phases spread drops evenly over width and time.
            const double xr = bb.left() - 0.3 + (bb.width() + 0.6) * (j + 0.2 + 0.6 * h01(seed, 1, id)) / drops;
            // A quarter pitch off the hex columns puts one LED of every row right under the drop.
            const double x = std::round((xr - 0.25) * 2.0) / 2.0 + 0.25;
            const int speed = cycles + (h01(seed, 3, id) < 0.5 ? 0 : 1);   // falls per loop
            const double start = fract(j * 0.6180339887 + 0.3 * h01(seed, 4, id));
            const double headY = lerp(y0, y1, fract(ctx.phase * speed + start));
            const double bright = 0.7 + 0.3 * h01(seed, 5, id);
            const Rgb col = pal.sample(h01(seed, 6, id));
            for (int k = 0; k < L.count(); ++k) {
                const QPointF p = L.led(k).pos;
                const double dx = p.x() - x;
                if (std::abs(dx) > 1.6)
                    continue;
                const double dy = headY - p.y();   // > 0: LED is above the head, in the trail
                if (dy < -3.5 * kHeadSigma || dy > len + 3.5 * kHeadSigma)
                    continue;
                const double head = gauss(dy, kHeadSigma);
                const double tu = dy / len;
                const double tail = dy > 0 && tu < 1.0 ? (1.0 - tu) * smooth(0.0, 0.5, 1.0 - tu) : 0.0;
                const double v = gauss(dx, 0.4) * std::max(head, 0.92 * tail) * bright;
                acc[size_t(k)].add(whiten(col, 0.3 * head * head * head), v);
            }
        }
        for (int k = 0; k < L.count(); ++k)
            out[size_t(k)] = overlayPixel(acc[size_t(k)].color(), acc[size_t(k)].level(), transparent, bg);
    }
};

class CometEffect : public Effect {
public:
    QString typeId() const override { return "comet"; }
    QString name() const override { return QT_TRANSLATE_NOOP("Effects", "Comet"); }
    QString category() const override { return kCategory; }
    QList<ParamDesc> params() const override
    {
        QList<ParamDesc> p{
            pGradient("gradient", QT_TRANSLATE_NOOP("Effects", "Colors (head → tail)"),
                      linearPalette({QColor(140, 245, 255), QColor(0, 200, 255), QColor(30, 90, 255),
                                     QColor(150, 40, 255)})),
            pInt("heads", QT_TRANSLATE_NOOP("Effects", "Comets"), 2, 1, 4),
            pPercent("tail", QT_TRANSLATE_NOOP("Effects", "Tail length"), 80),
            pFloat("width", QT_TRANSLATE_NOOP("Effects", "Width"), 1.3, 0.5, 3.0, 0.05),
            pChoice("path", QT_TRANSLATE_NOOP("Effects", "Path"),
                    {QT_TRANSLATE_NOOP("Effects", "Along the edge"), QT_TRANSLATE_NOOP("Effects", "Inner loop")}, 0),
            pChoice("direction", QT_TRANSLATE_NOOP("Effects", "Rotation"),
                    {QT_TRANSLATE_NOOP("Effects", "Clockwise"), QT_TRANSLATE_NOOP("Effects", "Counterclockwise")},
                    0),
            pCycles(1),
        };
        addOverlayParams(p, QColor(0, 0, 10));
        return p;
    }
    void render(const RenderContext& ctx, Frame& out) const override
    {
        const LedLayout& L = layoutOf(ctx);
        const ColorGradient pal = g("gradient");
        const int heads = i("heads");
        const double dir = i("direction") == 0 ? 1.0 : -1.0;
        const LoopPath path = i("path") == 0 ? perimeterPath(L) : innerPath(L);
        const double P = path.length;
        // Tail as a share of the gap to the next comet.
        const double tailLen = std::max(f("tail") / 100.0, 0.02) * P / heads;
        const double sigma = f("width") * 0.45;
        const bool transparent = b("transparent");
        const Rgb bg = c("background");
        const double run = dir * ctx.phase * i("cycles");
        const Rgb headCol = pal.sample(0.12);
        for (int k = 0; k < L.count(); ++k) {
            const double lateral = gauss(path.ledDist[size_t(k)], sigma);
            LightAccum acc;
            if (lateral > 1e-4) {
                for (int h = 0; h < heads; ++h) {
                    const double sh = fract(run + double(h) / heads);
                    const double behind = fract(dir * (sh - path.ledS[size_t(k)])) * P;
                    const double ahead = P - behind;
                    // Head and tail are separate lights, so nothing jumps where "behind" wraps at the head.
                    const double tu = behind / tailLen;
                    const double tail = tu < 1.0 ? (1.0 - tu) * smooth(0.0, 1.0, behind) : 0.0;
                    const double head = behind < ahead ? gauss(behind, 0.65) : gauss(ahead, 0.5);
                    acc.add(pal.sample(clamp01(0.15 + 0.85 * tu)), tail * lateral);
                    // White-hot only in the very core; a dim white fringe would read as grey.
                    const double hv = head * lateral;
                    acc.add(whiten(headCol, 0.65 * hv * hv * hv), hv);
                }
            }
            out[size_t(k)] = overlayPixel(acc.color(), acc.level(), transparent, bg);
        }
    }
};

class SparkleEffect : public Effect {
public:
    QString typeId() const override { return "sparkle"; }
    QString name() const override { return QT_TRANSLATE_NOOP("Effects", "Sparkle"); }
    QString category() const override { return kCategory; }
    QList<ParamDesc> params() const override
    {
        QList<ParamDesc> p{
            pGradient("gradient", QT_TRANSLATE_NOOP("Effects", "Colors (flash → fade)"),
                      linearPalette({QColor(255, 255, 255), QColor(255, 200, 90), QColor(255, 90, 20)})),
            pPercent("density", QT_TRANSLATE_NOOP("Effects", "Density"), 50),
            pFloat("size", QT_TRANSLATE_NOOP("Effects", "Size"), 0.9, 0.3, 2.0, 0.05),
            pCycles(2),
            pSeed(1),
        };
        addOverlayParams(p, QColor(0, 0, 0));
        return p;
    }
    void render(const RenderContext& ctx, Frame& out) const override
    {
        const LedLayout& L = layoutOf(ctx);
        const ColorGradient pal = g("gradient");
        const int slotCount = 6 + int(std::lround(f("density") / 100.0 * 60.0));
        const double sigma = f("size") * 0.45;
        const int rate = i("cycles") * 3;   // sparkles per slot per loop
        const double life = std::min(0.85 / rate, 0.08);
        const int seed = i("seed");
        const bool transparent = b("transparent");
        const Rgb bg = c("background");
        std::vector<LightAccum> acc(size_t(L.count()));
        for (int j = 0; j < slotCount; ++j) {
            const double x = ctx.phase * rate + h01(seed, 1, uint32_t(j));
            const double m = std::floor(x);
            const double u = (x - m) / rate / life;   // 0..1 over the sparkle's life
            if (u >= 1.0)
                continue;
            // Quick eased attack, long perceptual decay that ends exactly at 0.
            const double env = std::min(1.0, smooth(0.0, 0.2, u) * std::pow(1.0 - u, 1.6) / 0.72);
            const uint32_t id = uint32_t(j) * 1024U + uint32_t(lmod((long long)m, rate));
            const int ledIdx = std::min(L.count() - 1, int(h01(seed, 2, id) * L.count()));
            const QPointF c0 = L.led(ledIdx).pos
                               + QPointF(h01(seed, 3, id) - 0.5, h01(seed, 4, id) - 0.5) * 0.8;
            // Cools along the palette as it fades, and the halo runs cooler too, so dim fringes stay gold, not grey.
            const double heat = 0.12 * h01(seed, 5, id) + 0.95 * smooth(0.05, 0.75, u);
            for (int k = 0; k < L.count(); ++k) {
                const QPointF d = L.led(k).pos - c0;
                const double dist = std::hypot(d.x(), d.y());
                if (dist > 3.5 * sigma)
                    continue;
                const double gv = gauss(dist, sigma);
                acc[size_t(k)].add(pal.sample(clamp01(heat + 0.45 * (1.0 - gv))), env * gv);
            }
        }
        for (int k = 0; k < L.count(); ++k)
            out[size_t(k)] = overlayPixel(acc[size_t(k)].color(), acc[size_t(k)].level(), transparent, bg);
    }
};

class FirefliesEffect : public Effect {
public:
    QString typeId() const override { return "fireflies"; }
    QString name() const override { return QT_TRANSLATE_NOOP("Effects", "Fireflies"); }
    QString category() const override { return kCategory; }
    QList<ParamDesc> params() const override
    {
        QList<ParamDesc> p{
            pGradient("gradient", QT_TRANSLATE_NOOP("Effects", "Colors"),
                      linearPalette({QColor(170, 255, 40), QColor(255, 215, 50)})),
            pInt("count", QT_TRANSLATE_NOOP("Effects", "Count"), 8, 1, 16),
            pFloat("size", QT_TRANSLATE_NOOP("Effects", "Size"), 1.4, 0.5, 3.0, 0.05),
            pCycles(1),
            pSeed(1),
        };
        addOverlayParams(p, QColor(0, 6, 4));
        return p;
    }
    void render(const RenderContext& ctx, Frame& out) const override
    {
        const LedLayout& L = layoutOf(ctx);
        const ColorGradient pal = g("gradient");
        const int count = i("count");
        const double sigma = f("size") * 0.45;
        const int cycles = i("cycles");
        const int seed = i("seed");
        const bool transparent = b("transparent");
        const Rgb bg = c("background");
        const QRectF bb = L.bounds();
        const double yc = bb.center().y(), yh = bb.height() * 0.5;
        std::vector<LightAccum> acc(size_t(L.count()));
        for (int j = 0; j < count; ++j) {
            const uint32_t id = uint32_t(j);
            const QPointF home = L.led(std::min(L.count() - 1, int(h01(seed, 1, id) * L.count()))).pos;
            const int fxq = cycles * (1 + int(h01(seed, 2, id) * 1.999));
            const int fyq = cycles * (1 + int(h01(seed, 3, id) * 1.999));
            const double ax = 1.5 + 2.0 * h01(seed, 4, id), ay = 1.2 + 1.6 * h01(seed, 5, id);
            double y = home.y() + ay * std::sin(kTau * (fyq * ctx.phase + h01(seed, 6, id)));
            y = yc + yh * std::tanh((y - yc) / yh);   // soft containment
            const double hw = halfWidthAt(L, y) + 0.4;
            double x = home.x() + ax * std::sin(kTau * (fxq * ctx.phase + h01(seed, 7, id)));
            x = hw * std::tanh(x / hw);
            const int fp = cycles * (1 + int(h01(seed, 8, id) * 2.999));
            const double s = 0.5 - 0.5 * std::cos(kTau * (fp * ctx.phase + h01(seed, 9, id)));
            const double glow = std::pow(s, 1.3);
            const Rgb col = pal.sample(h01(seed, 10, id));
            for (int k = 0; k < L.count(); ++k) {
                const QPointF d = L.led(k).pos - QPointF(x, y);
                const double dist = std::hypot(d.x(), d.y());
                if (dist > 3.5 * sigma)
                    continue;
                acc[size_t(k)].add(col, glow * gauss(dist, sigma));
            }
        }
        for (int k = 0; k < L.count(); ++k)
            out[size_t(k)] = overlayPixel(acc[size_t(k)].color(), acc[size_t(k)].level(), transparent, bg);
    }
};

} // namespace

void registerParticleEffects(EffectRegistry& r)
{
    r.add<StarsEffect>();
    r.add<RainEffect>();
    r.add<CometEffect>();
    r.add<SparkleEffect>();
    r.add<FirefliesEffect>();
}

} // namespace fg
