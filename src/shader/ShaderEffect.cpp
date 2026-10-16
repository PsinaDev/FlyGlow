#include "ShaderEffect.h"

#include "ShaderRenderer.h"
#include "core/LedLayout.h"
#include "media/LedSampler.h"

#include <QCoreApplication>
#include <QThread>
#include <algorithm>

namespace fg {

namespace {

ParamDesc floatParam(const QString& id, const QString& label, double def, double min, double max, double step,
                     const QString& suffix)
{
    ParamDesc p;
    p.id = id;
    p.label = label;
    p.type = ParamDesc::Type::Float;
    p.def = def;
    p.min = min;
    p.max = max;
    p.step = step;
    p.suffix = suffix;
    return p;
}

const char* kDefaultSource = R"(// iTime: seconds. iPhase: 0..1 through the loop. iLoop: loop length in seconds.
// Animated through iPhase, a shader loops on its own; set the loop crossfade to 0 then.

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float a = 6.2831853 * iPhase;
    float rings = 0.5 + 0.5 * sin(14.0 * length(uv) - 2.0 * a);
    vec3 col = 0.5 + 0.5 * cos(a + atan(uv.y, uv.x) + vec3(0.0, 2.1, 4.2));
    fragColor = vec4(col * rings, 1.0);
}
)";

} // namespace

QString ShaderEffect::defaultSource()
{
    return QString::fromUtf8(kDefaultSource);
}

QString ShaderEffect::name() const
{
    return QT_TRANSLATE_NOOP("Effects", "Shader");
}

QString ShaderEffect::category() const
{
    return QT_TRANSLATE_NOOP("Effects", "Drawing");
}

QList<ParamDesc> ShaderEffect::params() const
{
    return {
        floatParam("speed", QT_TRANSLATE_NOOP("Effects", "Speed"), 1.0, 0.05, 4.0, 0.05, QStringLiteral("×")),
        floatParam("seam", QT_TRANSLATE_NOOP("Effects", "Loop crossfade"), 20, 0, 50, 1, QStringLiteral("%")),
        floatParam("brightness", QT_TRANSLATE_NOOP("Effects", "Brightness"), 100, 0, 300, 1, QStringLiteral("%")),
        floatParam("saturation", QT_TRANSLATE_NOOP("Effects", "Saturation"), 100, 0, 200, 1, QStringLiteral("%")),
        floatParam("zoom", QT_TRANSLATE_NOOP("Effects", "Zoom"), 1.0, 1.0, 4.0, 0.05, QStringLiteral("×")),
        floatParam("panX", QT_TRANSLATE_NOOP("Effects", "Shift right"), 0, -50, 50, 1, QStringLiteral("%")),
        floatParam("panY", QT_TRANSLATE_NOOP("Effects", "Shift down"), 0, -50, 50, 1, QStringLiteral("%")),
    };
}

void ShaderEffect::render(const RenderContext& ctx, Frame& out) const
{
    std::fill(out.begin(), out.end(), Rgba(0, 0, 0, 0));
    ShaderRenderer* renderer = ShaderRenderer::instance();
    if (!renderer || QThread::currentThread() != QCoreApplication::instance()->thread())
        return;
    const LedLayout& layout = ctx.layout ? *ctx.layout : LedLayout::cd2();
    const double span = ctx.loopSeconds * std::max(0.0, f("speed"));
    std::vector<Rgb> now;
    if (!sample(*renderer, layout, ctx.phase * span, ctx, now))
        return;

    // iTime would run on past the loop's end, so the last stretch fades into the moments just before
    // time zero, which lead straight into the first frame.
    const double seam = std::clamp(f("seam") / 100.0, 0.0, 0.5);
    std::vector<Rgb> before;
    if (seam > 0 && ctx.phase > 1.0 - seam && sample(*renderer, layout, (ctx.phase - 1.0) * span, ctx, before)) {
        const float w = float(fx::smooth(1.0 - seam, 1.0, ctx.phase));
        for (size_t i = 0; i < now.size() && i < before.size(); ++i)
            now[i] = {now[i].r + (before[i].r - now[i].r) * w, now[i].g + (before[i].g - now[i].g) * w,
                      now[i].b + (before[i].b - now[i].b) * w};
    }
    for (size_t i = 0; i < out.size() && i < now.size(); ++i)
        out[i] = Rgba(now[i], 1.f);
}

bool ShaderEffect::sample(ShaderRenderer& renderer, const LedLayout& layout, double time, const RenderContext& ctx,
                          std::vector<Rgb>& out) const
{
    ShaderTime t;
    t.time = time;
    t.phase = ctx.phase;
    t.loop = ctx.loopSeconds;
    const QImage image = renderer.render(m_source, t);
    if (image.isNull())
        return false;
    media::Framing framing;
    framing.fit = media::FitMode::Stretch;
    framing.zoom = std::max(1.0, f("zoom"));
    framing.panX = -f("panX") / 100.0;
    framing.panY = -f("panY") / 100.0;
    media::Adjustments adjust;
    adjust.brightness = std::max(0.0, f("brightness") / 100.0);
    adjust.saturation = std::max(0.0, f("saturation") / 100.0);
    media::LedSampler(layout).sample(image, framing, adjust, out);
    return true;
}

std::unique_ptr<Effect> ShaderEffect::clone() const
{
    auto copy = std::make_unique<ShaderEffect>();
    copy->m_values = m_values;
    copy->m_source = m_source;
    return copy;
}

QJsonObject ShaderEffect::toJson() const
{
    QJsonObject o = Effect::toJson();
    o.insert("source", m_source);
    return o;
}

void ShaderEffect::fromJson(const QJsonObject& o)
{
    Effect::fromJson(o);
    m_source = o.value("source").toString(defaultSource());
}

} // namespace fg
