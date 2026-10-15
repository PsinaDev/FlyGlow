#include "Effect.h"

#include <QJsonArray>
#include <QJsonValue>
#include <cmath>
#include <numbers>

namespace fg {

QVariant Effect::value(const QString& id) const
{
    auto it = m_values.constFind(id);
    if (it != m_values.constEnd())
        return *it;
    for (const ParamDesc& p : params())
        if (p.id == id)
            return p.def;
    return {};
}

void Effect::setValue(const QString& id, const QVariant& v)
{
    m_values.insert(id, v);
}

std::unique_ptr<Effect> Effect::clone() const
{
    std::unique_ptr<Effect> copy = EffectRegistry::instance().create(typeId());
    if (copy)
        copy->m_values = m_values;
    return copy;
}

QJsonObject Effect::toJson() const
{
    QJsonObject params;
    for (const ParamDesc& p : this->params()) {
        const QVariant v = value(p.id);
        switch (p.type) {
        case ParamDesc::Type::Color:
            params.insert(p.id, v.value<QColor>().name(QColor::HexRgb));
            break;
        case ParamDesc::Type::Gradient:
            params.insert(p.id, QJsonValue::fromVariant(ColorGradient::fromVariant(v).toVariant()));
            break;
        case ParamDesc::Type::Bool:
            params.insert(p.id, v.toBool());
            break;
        case ParamDesc::Type::Int:
        case ParamDesc::Type::Choice:
            params.insert(p.id, v.toInt());
            break;
        default:
            params.insert(p.id, v.toDouble());
            break;
        }
    }
    return QJsonObject{{"type", typeId()}, {"params", params}};
}

void Effect::fromJson(const QJsonObject& o)
{
    const QJsonObject params = o.value("params").toObject();
    for (const ParamDesc& p : this->params()) {
        if (!params.contains(p.id))
            continue;
        const QJsonValue v = params.value(p.id);
        switch (p.type) {
        case ParamDesc::Type::Color:
            setValue(p.id, QColor(v.toString()));
            break;
        case ParamDesc::Type::Gradient:
            setValue(p.id, QVariant::fromValue(ColorGradient::fromVariant(v.toVariant())));
            break;
        case ParamDesc::Type::Bool:
            setValue(p.id, v.toBool());
            break;
        case ParamDesc::Type::Int:
        case ParamDesc::Type::Choice:
            setValue(p.id, v.toInt());
            break;
        default:
            setValue(p.id, v.toDouble());
            break;
        }
    }
}

std::unique_ptr<Effect> Effect::createFromJson(const QJsonObject& o)
{
    std::unique_ptr<Effect> e = EffectRegistry::instance().create(o.value("type").toString());
    if (e)
        e->fromJson(o);
    return e;
}

EffectRegistry& EffectRegistry::instance()
{
    static EffectRegistry registry;
    return registry;
}

std::unique_ptr<Effect> EffectRegistry::create(const QString& typeId) const
{
    for (const Entry& e : m_entries)
        if (e.typeId == typeId)
            return e.make();
    return nullptr;
}

namespace fx {

static inline uint32_t mix32(uint32_t h)
{
    h ^= h >> 16;
    h *= 0x7feb352dU;
    h ^= h >> 15;
    h *= 0x846ca68bU;
    h ^= h >> 16;
    return h;
}

double hash01(uint32_t seed, uint32_t index)
{
    return mix32(seed * 0x9E3779B9U ^ mix32(index + 0x632BE5ABU)) / 4294967296.0;
}

double smooth(double e0, double e1, double x)
{
    if (e1 == e0)
        return x < e0 ? 0.0 : 1.0;
    double t = (x - e0) / (e1 - e0);
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return t * t * (3 - 2 * t);
}

double wrap01(double x)
{
    return x - std::floor(x);
}

double phaseDelta(double a, double b)
{
    double d = wrap01(a - b);
    return d >= 0.5 ? d - 1.0 : d;
}

static inline double lattice4(uint32_t seed, int x, int y, int z, int w)
{
    uint32_t h = seed;
    h = mix32(h ^ uint32_t(x) * 0x8DA6B343U);
    h = mix32(h ^ uint32_t(y) * 0xD8163841U);
    h = mix32(h ^ uint32_t(z) * 0xCB1AB31FU);
    h = mix32(h ^ uint32_t(w) * 0x165667B1U);
    return h / 4294967296.0;
}

static inline double fade(double t)
{
    return t * t * t * (t * (t * 6 - 15) + 10);
}

static double valueNoise4(double x, double y, double z, double w, uint32_t seed)
{
    const int xi = int(std::floor(x)), yi = int(std::floor(y)), zi = int(std::floor(z)),
              wi = int(std::floor(w));
    const double fx = fade(x - xi), fy = fade(y - yi), fz = fade(z - zi), fw = fade(w - wi);
    double acc[2];
    for (int dw = 0; dw < 2; ++dw) {
        double zs[2];
        for (int dz = 0; dz < 2; ++dz) {
            double ys[2];
            for (int dy = 0; dy < 2; ++dy) {
                const double a = lattice4(seed, xi, yi + dy, zi + dz, wi + dw);
                const double b = lattice4(seed, xi + 1, yi + dy, zi + dz, wi + dw);
                ys[dy] = a + (b - a) * fx;
            }
            zs[dz] = ys[0] + (ys[1] - ys[0]) * fy;
        }
        acc[dw] = zs[0] + (zs[1] - zs[0]) * fz;
    }
    return acc[0] + (acc[1] - acc[0]) * fw;
}

double loopNoise(double x, double y, double phase, uint32_t seed, double timeRadius)
{
    // Time walks a circle in the (z, w) plane, so phase 0 and 1 meet exactly.
    const double a = 2.0 * std::numbers::pi * phase;
    return valueNoise4(x, y, timeRadius * std::cos(a) + 17.3, timeRadius * std::sin(a) + 41.9,
                       seed);
}

} // namespace fx

} // namespace fg
