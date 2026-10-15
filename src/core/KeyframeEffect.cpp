#include "KeyframeEffect.h"

#include "LedLayout.h"

#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace fg {

namespace {

QString encodeColor(const Rgba& c)
{
    const QColor q = color::toQColor(c);
    return QString::asprintf("%02x%02x%02x%02x", q.red(), q.green(), q.blue(), q.alpha());
}

Rgba decodeColor(const QString& s)
{
    if (s.size() < 8)
        return {};
    bool ok = false;
    const uint v = s.left(8).toUInt(&ok, 16);
    if (!ok)
        return {};
    const QColor q((v >> 24) & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff);
    return Rgba(color::fromQColor(q), float((v & 0xff) / 255.0));
}

double ease(KeyframeEffect::Easing e, double t)
{
    switch (e) {
    case KeyframeEffect::Easing::Linear:
        return t;
    case KeyframeEffect::Easing::Hold:
        return 0.0;
    case KeyframeEffect::Easing::Smooth:
    default:
        return t * t * (3.0 - 2.0 * t);
    }
}

} // namespace

KeyframeEffect::KeyframeEffect()
{
    Keyframe k;
    k.time = 0;
    k.colors.assign(size_t(LedLayout::cd2().count()), Rgba(0, 0, 0, 0));
    m_keys.push_back(k);
}

void KeyframeEffect::render(const RenderContext& ctx, Frame& out) const
{
    const size_t n = out.size();
    if (m_keys.empty()) {
        std::fill(out.begin(), out.end(), Rgba(0, 0, 0, 0));
        return;
    }
    if (m_keys.size() == 1) {
        for (size_t i = 0; i < n; ++i)
            out[i] = i < m_keys[0].colors.size() ? m_keys[0].colors[i] : Rgba();
        return;
    }

    const double t = ctx.phase - std::floor(ctx.phase);
    size_t ia = m_keys.size() - 1;
    for (size_t k = 0; k < m_keys.size(); ++k)
        if (m_keys[k].time <= t)
            ia = k;
    const size_t ib = (ia + 1) % m_keys.size();
    const Keyframe& a = m_keys[ia];
    const Keyframe& b = m_keys[ib];
    double span = b.time - a.time;
    double into = t - a.time;
    if (span <= 0)
        span += 1.0;
    if (into < 0)
        into += 1.0;
    const float k = float(ease(a.easing, span > 1e-9 ? std::clamp(into / span, 0.0, 1.0) : 0.0));

    for (size_t i = 0; i < n; ++i) {
        const Rgba ca = i < a.colors.size() ? a.colors[i] : Rgba();
        const Rgba cb = i < b.colors.size() ? b.colors[i] : Rgba();
        out[i] = k <= 0.f ? ca : (k >= 1.f ? cb : color::mix(ca, cb, k));
    }
}

std::unique_ptr<Effect> KeyframeEffect::clone() const
{
    auto copy = std::make_unique<KeyframeEffect>();
    copy->m_values = m_values;
    copy->m_keys = m_keys;
    return copy;
}

QJsonObject KeyframeEffect::toJson() const
{
    QJsonObject o = Effect::toJson();
    QJsonArray keys;
    for (const Keyframe& k : m_keys) {
        QJsonArray colors;
        for (const Rgba& c : k.colors)
            colors.append(encodeColor(c));
        keys.append(QJsonObject{{"time", k.time}, {"easing", int(k.easing)}, {"colors", colors}});
    }
    o.insert("keys", keys);
    return o;
}

void KeyframeEffect::fromJson(const QJsonObject& o)
{
    Effect::fromJson(o);
    const QJsonArray keys = o.value("keys").toArray();
    if (keys.isEmpty())
        return;
    const size_t n = size_t(LedLayout::cd2().count());
    m_keys.clear();
    for (const QJsonValue& kv : keys) {
        const QJsonObject ko = kv.toObject();
        Keyframe k;
        k.time = std::clamp(ko.value("time").toDouble(), 0.0, 0.999999);
        k.easing = Easing(std::clamp(ko.value("easing").toInt(), 0, 2));
        const QJsonArray colors = ko.value("colors").toArray();
        k.colors.assign(n, Rgba());
        for (size_t i = 0; i < n && i < size_t(colors.size()); ++i)
            k.colors[i] = decodeColor(colors[qsizetype(i)].toString());
        m_keys.push_back(std::move(k));
    }
    sortKeys();
}

int KeyframeEffect::keyAt(double time, double tolerance) const
{
    for (size_t i = 0; i < m_keys.size(); ++i)
        if (std::abs(fx::phaseDelta(m_keys[i].time, time)) <= tolerance)
            return int(i);
    return -1;
}

int KeyframeEffect::addKey(double time)
{
    time = fx::wrap01(time);
    const int existing = keyAt(time);
    if (existing >= 0)
        return existing;
    Keyframe k;
    k.time = time;
    k.colors.assign(size_t(LedLayout::cd2().count()), Rgba());
    RenderContext ctx;
    ctx.phase = time;
    ctx.layout = &LedLayout::cd2();
    render(ctx, k.colors);
    m_keys.push_back(std::move(k));
    sortKeys();
    return keyAt(time, 1e-9);
}

void KeyframeEffect::removeKey(int index)
{
    if (m_keys.size() <= 1 || index < 0 || index >= keyCount())
        return;
    m_keys.erase(m_keys.begin() + index);
}

int KeyframeEffect::moveKey(int index, double time)
{
    if (index < 0 || index >= keyCount())
        return index;
    time = std::clamp(time, 0.0, 0.999);
    for (int i = 0; i < keyCount(); ++i)
        if (i != index && std::abs(m_keys[size_t(i)].time - time) < 0.002)
            return index;
    m_keys[size_t(index)].time = time;
    const double marker = time;
    sortKeys();
    for (int i = 0; i < keyCount(); ++i)
        if (m_keys[size_t(i)].time == marker)
            return i;
    return index;
}

void KeyframeEffect::setEasing(int index, Easing e)
{
    if (index >= 0 && index < keyCount())
        m_keys[size_t(index)].easing = e;
}

void KeyframeEffect::setLed(int key, int led, const Rgba& c)
{
    if (key < 0 || key >= keyCount())
        return;
    auto& colors = m_keys[size_t(key)].colors;
    if (led >= 0 && size_t(led) < colors.size())
        colors[size_t(led)] = c;
}

void KeyframeEffect::fill(int key, const Rgba& c)
{
    if (key < 0 || key >= keyCount())
        return;
    auto& colors = m_keys[size_t(key)].colors;
    std::fill(colors.begin(), colors.end(), c);
}

void KeyframeEffect::sortKeys()
{
    std::stable_sort(m_keys.begin(), m_keys.end(),
                     [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
}

} // namespace fg
