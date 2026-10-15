#include "Color.h"

#include <QVariantMap>
#include <algorithm>
#include <cmath>

namespace fg {

static Lab lerp(const Lab& a, const Lab& b, float t)
{
    return {a.L + (b.L - a.L) * t, a.a + (b.a - a.a) * t, a.b + (b.b - a.b) * t};
}

namespace color {

float srgbToLinear(float v)
{
    v = std::clamp(v, 0.f, 1.f);
    return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float v)
{
    v = std::clamp(v, 0.f, 1.f);
    return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.f / 2.4f) - 0.055f;
}

Rgb fromQColor(const QColor& c)
{
    return {srgbToLinear(float(c.redF())), srgbToLinear(float(c.greenF())),
            srgbToLinear(float(c.blueF()))};
}

QColor toQColor(const Rgb& c)
{
    return QColor::fromRgbF(linearToSrgb(c.r), linearToSrgb(c.g), linearToSrgb(c.b));
}

QColor toQColor(const Rgba& c)
{
    return QColor::fromRgbF(linearToSrgb(c.r), linearToSrgb(c.g), linearToSrgb(c.b),
                            std::clamp(c.a, 0.f, 1.f));
}

Lab toOklab(const Rgb& c)
{
    const float l = 0.4122214708f * c.r + 0.5363325363f * c.g + 0.0514459929f * c.b;
    const float m = 0.2119034982f * c.r + 0.6806995451f * c.g + 0.1073969566f * c.b;
    const float s = 0.0883024619f * c.r + 0.2817188376f * c.g + 0.6299787005f * c.b;
    const float l_ = std::cbrt(l), m_ = std::cbrt(m), s_ = std::cbrt(s);
    return {0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
            1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
            0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_};
}

Rgb fromOklab(const Lab& c)
{
    const float l_ = c.L + 0.3963377774f * c.a + 0.2158037573f * c.b;
    const float m_ = c.L - 0.1055613458f * c.a - 0.0638541728f * c.b;
    const float s_ = c.L - 0.0894841775f * c.a - 1.2914855480f * c.b;
    const float l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
    return clamp01({+4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
                    -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
                    -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s});
}

Rgb mix(const Rgb& a, const Rgb& b, float t)
{
    if (t <= 0.f)
        return a;
    if (t >= 1.f)
        return b;
    return fromOklab(lerp(toOklab(a), toOklab(b), t));
}

Rgba mix(const Rgba& a, const Rgba& b, float t)
{
    t = std::clamp(t, 0.f, 1.f);
    const float alpha = a.a + (b.a - a.a) * t;
    if (alpha <= 1e-6f)
        return {0, 0, 0, 0};
    // Alpha-weighted, so a transparent end contributes no hue of its own.
    const float wa = a.a * (1.f - t), wb = b.a * t;
    const Lab la = toOklab(a.rgb()), lb = toOklab(b.rgb());
    const float k = wb / (wa + wb);
    return Rgba(fromOklab(lerp(la, lb, k)), alpha);
}

float linearToPerceptual(float x)
{
    return x <= 0.f ? 0.f : std::cbrt(x);
}

Rgb dim(const Rgb& c, float perceived)
{
    const float k = perceptualToLinear(std::clamp(perceived, 0.f, 1.f));
    return {c.r * k, c.g * k, c.b * k};
}

Rgb rainbow(float h)
{
    static const ColorGradient g = ColorGradient::rainbow();
    return g.sample(h, true);
}

Rgb clamp01(const Rgb& c)
{
    return {std::clamp(c.r, 0.f, 1.f), std::clamp(c.g, 0.f, 1.f), std::clamp(c.b, 0.f, 1.f)};
}

float luminance(const Rgb& c)
{
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

} // namespace color

ColorGradient::ColorGradient()
    : ColorGradient({QColor(0, 116, 255), QColor(195, 32, 230)})
{
}

ColorGradient::ColorGradient(QVector<GradientStop> stops)
{
    setStops(std::move(stops));
}

ColorGradient::ColorGradient(std::initializer_list<QColor> evenlySpaced)
{
    QVector<GradientStop> stops;
    const int n = int(evenlySpaced.size());
    int i = 0;
    for (const QColor& c : evenlySpaced)
        stops.push_back({n > 1 ? double(i++) / (n - 1) : 0.0, c});
    setStops(std::move(stops));
}

ColorGradient ColorGradient::rainbow()
{
    // Stops spaced by OKLab distance so the sweep doesn't linger on green or rush through yellow.
    const QColor hues[] = {QColor(255, 0, 0),   QColor(255, 255, 0), QColor(0, 255, 0),
                           QColor(0, 255, 255), QColor(0, 0, 255),   QColor(255, 0, 255)};
    const int n = 6;
    double dist[n + 1] = {0};
    for (int i = 0; i < n; ++i) {
        const Lab a = color::toOklab(color::fromQColor(hues[i]));
        const Lab b = color::toOklab(color::fromQColor(hues[(i + 1) % n]));
        dist[i + 1] = dist[i] + std::sqrt(double((a.L - b.L) * (a.L - b.L) + (a.a - b.a) * (a.a - b.a)
                                                 + (a.b - b.b) * (a.b - b.b)));
    }
    QVector<GradientStop> stops;
    for (int i = 0; i < n; ++i)
        stops.push_back({dist[i] / dist[n], hues[i]});
    return ColorGradient(stops);
}

void ColorGradient::setStops(QVector<GradientStop> stops)
{
    std::stable_sort(stops.begin(), stops.end(),
                     [](const GradientStop& a, const GradientStop& b) { return a.pos < b.pos; });
    if (stops.isEmpty())
        stops.push_back({0.0, QColor(Qt::white)});
    m_stops = std::move(stops);
    rebuild();
}

void ColorGradient::rebuild()
{
    m_lab.clear();
    m_lab.reserve(m_stops.size());
    for (const GradientStop& s : m_stops)
        m_lab.push_back(color::toOklab(color::fromQColor(s.color)));
}

Rgb ColorGradient::sample(double t, bool cyclic) const
{
    const int n = int(m_stops.size());
    if (n == 1)
        return color::fromOklab(m_lab[0]);
    if (cyclic) {
        t -= std::floor(t);
        // Stops at both 0 and 1 leave the wrap no room, so squeeze them into [0, (n-1)/n].
        if (m_stops.front().pos <= 1e-6 && m_stops.back().pos >= 1.0 - 1e-6) {
            const double squeeze = double(n - 1) / n;
            if (t >= squeeze) {
                const float k = float((t - squeeze) / (1.0 - squeeze));
                return color::fromOklab(lerp(m_lab[n - 1], m_lab[0], k));
            }
            t /= squeeze;
            for (int i = 0; i + 1 < n; ++i) {
                const double a = m_stops[i].pos, b = m_stops[i + 1].pos;
                if (t >= a && t <= b) {
                    const float k = b - a > 1e-9 ? float((t - a) / (b - a)) : 0.f;
                    return color::fromOklab(lerp(m_lab[i], m_lab[i + 1], k));
                }
            }
            return color::fromOklab(m_lab[n - 1]);
        }
        const double first = m_stops.front().pos, last = m_stops.back().pos;
        const double wrap = 1.0 - last + first;
        if (t < first || t >= last) {
            double u = t >= last ? t - last : t + 1.0 - last;
            const float k = wrap > 1e-9 ? float(u / wrap) : 0.f;
            return color::fromOklab(lerp(m_lab[n - 1], m_lab[0], k));
        }
    } else {
        if (t <= m_stops.front().pos)
            return color::fromOklab(m_lab.front());
        if (t >= m_stops.back().pos)
            return color::fromOklab(m_lab.back());
    }
    for (int i = 0; i + 1 < n; ++i) {
        const double a = m_stops[i].pos, b = m_stops[i + 1].pos;
        if (t >= a && t <= b) {
            const float k = b - a > 1e-9 ? float((t - a) / (b - a)) : 0.f;
            return color::fromOklab(lerp(m_lab[i], m_lab[i + 1], k));
        }
    }
    return color::fromOklab(m_lab.back());
}

QVariant ColorGradient::toVariant() const
{
    QVariantList list;
    for (const GradientStop& s : m_stops)
        list.push_back(QVariantMap{{"pos", s.pos}, {"color", s.color.name(QColor::HexRgb)}});
    return list;
}

ColorGradient ColorGradient::fromVariant(const QVariant& v)
{
    if (v.canConvert<ColorGradient>() && v.metaType() == QMetaType::fromType<ColorGradient>())
        return v.value<ColorGradient>();
    QVector<GradientStop> stops;
    for (const QVariant& item : v.toList()) {
        const QVariantMap m = item.toMap();
        stops.push_back({m.value("pos").toDouble(), QColor(m.value("color").toString())});
    }
    return stops.isEmpty() ? ColorGradient() : ColorGradient(stops);
}

} // namespace fg
