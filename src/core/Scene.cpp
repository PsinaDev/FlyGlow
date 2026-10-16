#include "Scene.h"

#include "LedLayout.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace fg {

QStringList blendModeNames()
{
    return {QT_TRANSLATE_NOOP("Effects", "Normal"), QT_TRANSLATE_NOOP("Effects", "Add"),
            QT_TRANSLATE_NOOP("Effects", "Screen"), QT_TRANSLATE_NOOP("Effects", "Multiply"),
            QT_TRANSLATE_NOOP("Effects", "Lighten")};
}

std::unique_ptr<Layer> Layer::clone() const
{
    auto l = std::make_unique<Layer>();
    l->name = name;
    l->visible = visible;
    l->opacity = opacity;
    l->blend = blend;
    l->effect = effect ? effect->clone() : nullptr;
    l->mask = mask;
    return l;
}

Scene::Scene() = default;

void Scene::render(double phase, const LedLayout& layout, std::vector<Rgb>& out) const
{
    const int n = layout.count();
    out.assign(size_t(n), Rgb{});
    Frame frame(static_cast<size_t>(n));
    RenderContext ctx;
    ctx.phase = phase - std::floor(phase);
    ctx.loopSeconds = loopSeconds;
    ctx.layout = &layout;

    for (const auto& layer : layers) {
        if (!layer->visible || !layer->effect || layer->opacity <= 0.0)
            continue;
        std::fill(frame.begin(), frame.end(), Rgba());
        layer->effect->render(ctx, frame);
        const float opacity = float(layer->opacity);
        for (int i = 0; i < n; ++i) {
            const Rgba& s = frame[size_t(i)];
            const float a = std::clamp(s.a, 0.f, 1.f) * opacity * layer->maskAt(i);
            if (a <= 0.f)
                continue;
            Rgb& d = out[size_t(i)];
            switch (layer->blend) {
            case BlendMode::Normal:
                d.r += (s.r - d.r) * a;
                d.g += (s.g - d.g) * a;
                d.b += (s.b - d.b) * a;
                break;
            case BlendMode::Add:
                d.r += s.r * a;
                d.g += s.g * a;
                d.b += s.b * a;
                break;
            case BlendMode::Screen:
                d.r = 1.f - (1.f - d.r) * (1.f - s.r * a);
                d.g = 1.f - (1.f - d.g) * (1.f - s.g * a);
                d.b = 1.f - (1.f - d.b) * (1.f - s.b * a);
                break;
            case BlendMode::Multiply:
                d.r *= 1.f + (s.r - 1.f) * a;
                d.g *= 1.f + (s.g - 1.f) * a;
                d.b *= 1.f + (s.b - 1.f) * a;
                break;
            case BlendMode::Lighten:
                d.r = std::max(d.r, s.r * a + d.r * (1.f - a));
                d.g = std::max(d.g, s.g * a + d.g * (1.f - a));
                d.b = std::max(d.b, s.b * a + d.b * (1.f - a));
                break;
            }
        }
    }
    for (Rgb& c : out)
        c = color::clamp01(c);
}

bool Scene::isAnimated() const
{
    for (const auto& layer : layers)
        if (layer->visible && layer->effect && layer->opacity > 0 && layer->effect->isAnimated())
            return true;
    return false;
}

QJsonObject Scene::toJson() const
{
    QJsonArray arr;
    for (const auto& layer : layers) {
        QJsonObject lo{{"name", layer->name},
                       {"visible", layer->visible},
                       {"opacity", layer->opacity},
                       {"blend", int(layer->blend)}};
        if (layer->effect)
            lo.insert("effect", layer->effect->toJson());
        if (!layer->mask.empty()) {
            QJsonArray m;
            for (float w : layer->mask)
                m.append(std::round(w * 1000.0) / 1000.0);
            lo.insert("mask", m);
        }
        arr.append(lo);
    }
    return QJsonObject{{"format", "flyglow"},
                       {"version", 1},
                       {"loopSeconds", loopSeconds},
                       {"brightness", brightness},
                       {"outputGamma", outputGamma},
                       {"dither", dither},
                       {"layers", arr}};
}

bool Scene::fromJson(const QJsonObject& o, QString* error)
{
    if (o.value("format").toString() != "flyglow") {
        if (error)
            *error = QCoreApplication::translate("Scene", "This is not a FlyGlow file.");
        return false;
    }
    std::vector<std::unique_ptr<Layer>> loaded;
    for (const QJsonValue& v : o.value("layers").toArray()) {
        const QJsonObject lo = v.toObject();
        auto layer = std::make_unique<Layer>();
        layer->name = lo.value("name").toString();
        layer->visible = lo.value("visible").toBool(true);
        layer->opacity = std::clamp(lo.value("opacity").toDouble(1.0), 0.0, 1.0);
        layer->blend = BlendMode(std::clamp(lo.value("blend").toInt(), 0, 4));
        layer->effect = Effect::createFromJson(lo.value("effect").toObject());
        if (!layer->effect)
            continue;
        const QJsonArray m = lo.value("mask").toArray();
        if (!m.isEmpty()) {
            layer->mask.assign(size_t(LedLayout::cd2().count()), 1.f);
            for (qsizetype i = 0; i < m.size() && size_t(i) < layer->mask.size(); ++i)
                layer->mask[size_t(i)] = float(std::clamp(m[i].toDouble(), 0.0, 1.0));
        }
        loaded.push_back(std::move(layer));
    }
    layers = std::move(loaded);
    loopSeconds = std::clamp(o.value("loopSeconds").toDouble(4.0), 0.2, 60.0);
    brightness = std::clamp(o.value("brightness").toInt(100), 1, 100);
    outputGamma = std::clamp(o.value("outputGamma").toDouble(kDockGamma), 0.5, 3.0);
    dither = o.value("dither").toBool(true);
    return true;
}

void Scene::copyFrom(const Scene& other)
{
    fromJson(other.toJson());
}

float encodeOutput(float linear, double gamma)
{
    const float v = std::clamp(linear, 0.f, 1.f);
    return 255.f * (gamma == 1.0 ? v : float(std::pow(v, 1.0 / gamma)));
}

void bakeTiming(const Scene& scene, int maxFrames, int* frameCount, int* period)
{
    maxFrames = std::clamp(maxFrames, 2, 255);
    if (!scene.isAnimated()) {
        *frameCount = 1;
        *period = 1;
        return;
    }
    const double ms = scene.loopSeconds * 1000.0;
    *period = std::clamp(int(std::ceil(ms / (20.0 * maxFrames))), 1, 255);
    *frameCount = std::clamp(int(std::lround(ms / (20.0 * *period))), 2, maxFrames);
    // N % 25 == 4 makes the blob a multiple of 50 bytes, and Space Station then announces one HID pack too many.
    if (*frameCount % 25 == 4)
        *frameCount += *frameCount < maxFrames ? 1 : -1;
}

BakeResult bake(const Scene& scene, const LedLayout& layout, int maxFrames)
{
    BakeResult r;
    const int n = layout.count();
    bakeTiming(scene, maxFrames, &r.frameCount, &r.period);
    r.loopSeconds = r.frameCount * r.period * 0.02;

    std::vector<std::vector<Rgb>> frames(size_t(r.frameCount));
    for (int k = 0; k < r.frameCount; ++k)
        scene.render(double(k) / r.frameCount, layout, frames[size_t(k)]);

    r.rgb.resize(qsizetype(r.frameCount) * n * 3);
    auto* outBytes = reinterpret_cast<uchar*>(r.rgb.data());
    const bool dither = scene.dither && r.frameCount > 1;
    std::vector<float> err;
    std::vector<uchar> warmup(size_t(n) * 3);

    // The first dither pass only warms up the error the loop carries round into frame 0.
    for (int pass = dither ? 0 : 1; pass < 2; ++pass)
        for (int k = 0; k < r.frameCount; ++k)
            encodeFrame(frames[size_t(k)], scene.outputGamma, dither ? &err : nullptr,
                        pass == 1 ? outBytes + size_t(k) * size_t(n) * 3 : warmup.data());
    return r;
}

void encodeFrame(const std::vector<Rgb>& frame, double gamma, std::vector<float>* err, uchar* out)
{
    if (err && err->size() != frame.size() * 3)
        err->assign(frame.size() * 3, 0.f);
    for (size_t i = 0; i < frame.size(); ++i) {
        const float ch[3] = {frame[i].r, frame[i].g, frame[i].b};
        for (size_t c = 0; c < 3; ++c) {
            const size_t at = i * 3 + c;
            float v = encodeOutput(ch[c], gamma);
            if (err)
                v += (*err)[at];
            const float q = std::clamp(std::round(v), 0.f, 255.f);
            if (err)
                (*err)[at] = std::clamp(v - q, -0.5f, 0.5f);
            out[at] = uchar(q);
        }
    }
}

} // namespace fg
