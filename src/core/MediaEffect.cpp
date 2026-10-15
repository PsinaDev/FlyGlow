#include "MediaEffect.h"

#include "LedLayout.h"

#include <QJsonArray>
#include <algorithm>
#include <array>
#include <cmath>

namespace fg {

namespace {

// Longest crossfade in seconds of real playback; longer frames hold and blend only at their end.
constexpr double kMaxFadeSeconds = 0.25;

const std::array<float, 256>& srgbLut()
{
    static const std::array<float, 256> lut = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i)
            t[size_t(i)] = color::srgbToLinear(i / 255.f);
        return t;
    }();
    return lut;
}

uchar toByte(float linear)
{
    return uchar(std::clamp(int(std::lround(color::linearToSrgb(linear) * 255.f)), 0, 255));
}

Rgb quantize(const Rgb& c)
{
    const auto& lut = srgbLut();
    return {lut[toByte(c.r)], lut[toByte(c.g)], lut[toByte(c.b)]};
}

QString encodeFrame(const std::vector<Rgb>& frame)
{
    static constexpr char kHex[] = "0123456789abcdef";
    QByteArray s(qsizetype(frame.size()) * 6, Qt::Uninitialized);
    char* p = s.data();
    for (const Rgb& c : frame) {
        for (float ch : {c.r, c.g, c.b}) {
            const uchar b = toByte(ch);
            *p++ = kHex[b >> 4];
            *p++ = kHex[b & 15];
        }
    }
    return QString::fromLatin1(s);
}

int hexDigit(QChar ch)
{
    const char16_t u = ch.unicode();
    if (u >= '0' && u <= '9')
        return u - '0';
    if (u >= 'a' && u <= 'f')
        return u - 'a' + 10;
    if (u >= 'A' && u <= 'F')
        return u - 'A' + 10;
    return 0;
}

std::vector<Rgb> decodeFrame(const QString& s, size_t leds)
{
    const auto& lut = srgbLut();
    std::vector<Rgb> frame(leds);
    for (size_t i = 0; i < leds && qsizetype(i * 6 + 6) <= s.size(); ++i) {
        uchar b[3];
        for (int c = 0; c < 3; ++c) {
            const qsizetype at = qsizetype(i * 6) + c * 2;
            b[c] = uchar(hexDigit(s[at]) * 16 + hexDigit(s[at + 1]));
        }
        frame[i] = {lut[b[0]], lut[b[1]], lut[b[2]]};
    }
    return frame;
}

} // namespace

QString MediaEffect::name() const
{
    return QT_TRANSLATE_NOOP("Effects", "Video / GIF");
}

QString MediaEffect::category() const
{
    return QT_TRANSLATE_NOOP("Effects", "Media");
}

QList<ParamDesc> MediaEffect::params() const
{
    ParamDesc smooth;
    smooth.id = "smooth";
    smooth.label = QT_TRANSLATE_NOOP("Effects", "Smooth frames");
    smooth.type = ParamDesc::Type::Bool;
    smooth.def = true;
    smooth.min = 0;
    smooth.max = 1;
    smooth.step = 1;

    ParamDesc cycles;
    cycles.id = "cycles";
    cycles.label = QT_TRANSLATE_NOOP("Effects", "Plays per loop");
    cycles.type = ParamDesc::Type::Int;
    cycles.def = 1;
    cycles.min = 1;
    cycles.max = 8;
    cycles.step = 1;
    cycles.suffix = QStringLiteral("×");

    return {smooth, cycles};
}

void MediaEffect::render(const RenderContext& ctx, Frame& out) const
{
    const size_t count = m_frames.size();
    if (count == 0 || m_total <= 0) {
        std::fill(out.begin(), out.end(), Rgba(0, 0, 0, 0));
        return;
    }
    const int cycles = std::clamp(i("cycles"), 1, 8);
    const double t = fx::wrap01(ctx.phase * cycles) * m_total;
    const auto next = std::upper_bound(m_starts.begin(), m_starts.end(), t);
    const size_t a = next == m_starts.begin() ? 0 : std::min(count - 1, size_t(next - m_starts.begin()) - 1);

    float k = 0.f;
    if (count > 1 && b("smooth")) {
        const double d = m_durations[a];
        const double clipPerRealSecond = ctx.loopSeconds > 0 ? m_total * cycles / ctx.loopSeconds : 1.0;
        const double fade = std::min(d, kMaxFadeSeconds * clipPerRealSecond);
        k = float(std::clamp((t - m_starts[a] - (d - fade)) / fade, 0.0, 1.0));
    }

    const std::vector<Rgb>& fa = m_frames[a];
    const std::vector<Rgb>& fb = m_frames[(a + 1) % count];
    for (size_t led = 0; led < out.size(); ++led) {
        const Rgb ca = led < fa.size() ? fa[led] : Rgb{};
        if (k <= 0.f) {
            out[led] = Rgba(ca);
            continue;
        }
        const Rgb cb = led < fb.size() ? fb[led] : Rgb{};
        out[led] = Rgba(color::mix(ca, cb, k));
    }
}

std::unique_ptr<Effect> MediaEffect::clone() const
{
    auto copy = std::make_unique<MediaEffect>();
    copy->m_values = m_values;
    copy->m_frames = m_frames;
    copy->m_durations = m_durations;
    copy->m_starts = m_starts;
    copy->m_total = m_total;
    return copy;
}

QJsonObject MediaEffect::toJson() const
{
    QJsonObject o = Effect::toJson();
    QJsonArray frames, durations;
    for (const std::vector<Rgb>& f : m_frames)
        frames.append(encodeFrame(f));
    for (double d : m_durations)
        durations.append(std::round(d * 1e6) / 1e6);
    o.insert("frames", frames);
    o.insert("durations", durations);
    return o;
}

void MediaEffect::fromJson(const QJsonObject& o)
{
    Effect::fromJson(o);
    const QJsonArray frames = o.value("frames").toArray();
    const QJsonArray durations = o.value("durations").toArray();
    const size_t leds = size_t(LedLayout::cd2().count());
    const qsizetype n = std::min<qsizetype>(frames.size(), kMaxFrames);
    std::vector<std::vector<Rgb>> f;
    std::vector<double> d;
    f.reserve(size_t(n));
    d.reserve(size_t(n));
    for (qsizetype k = 0; k < n; ++k) {
        f.push_back(decodeFrame(frames[k].toString(), leds));
        d.push_back(k < durations.size() ? durations[k].toDouble(0.1) : 0.1);
    }
    setClip(std::move(f), std::move(d));
}

void MediaEffect::setClip(std::vector<std::vector<Rgb>> frames, std::vector<double> durations)
{
    durations.resize(frames.size(), 0.1);
    for (double& d : durations)
        d = std::isfinite(d) && d > 0 ? std::max(d, 0.001) : 0.1;

    if (frames.size() > size_t(kMaxFrames)) {
        double total = 0;
        for (double d : durations)
            total += d;
        std::vector<std::vector<Rgb>> picked;
        picked.reserve(size_t(kMaxFrames));
        size_t src = 0;
        double srcStart = 0;
        for (int slot = 0; slot < kMaxFrames; ++slot) {
            const double mid = (slot + 0.5) * total / kMaxFrames;
            while (src + 1 < frames.size() && srcStart + durations[src] <= mid)
                srcStart += durations[src++];
            picked.push_back(frames[src]);
        }
        frames = std::move(picked);
        durations.assign(size_t(kMaxFrames), total / kMaxFrames);
    }

    const size_t leds = size_t(LedLayout::cd2().count());
    for (std::vector<Rgb>& f : frames) {
        f.resize(leds);
        for (Rgb& c : f)
            c = quantize(c);
    }
    m_frames = std::move(frames);
    m_durations = std::move(durations);
    rebuildTimeline();
}

void MediaEffect::rebuildTimeline()
{
    m_starts.resize(m_durations.size());
    m_total = 0;
    for (size_t k = 0; k < m_durations.size(); ++k) {
        m_starts[k] = m_total;
        m_total += m_durations[k];
    }
}

} // namespace fg
