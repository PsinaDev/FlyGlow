#include "EffectsInternal.h"

namespace fg::fxi {

ParamDesc pFloat(const QString& id, const QString& label, double def, double min, double max,
                 double step, const QString& suffix)
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

ParamDesc pPercent(const QString& id, const QString& label, double def)
{
    return pFloat(id, label, def, 0, 100, 1, "%");
}

ParamDesc pInt(const QString& id, const QString& label, int def, int min, int max,
               const QString& suffix)
{
    ParamDesc p;
    p.id = id;
    p.label = label;
    p.type = ParamDesc::Type::Int;
    p.def = def;
    p.min = min;
    p.max = max;
    p.step = 1;
    p.suffix = suffix;
    return p;
}

ParamDesc pBool(const QString& id, const QString& label, bool def)
{
    ParamDesc p;
    p.id = id;
    p.label = label;
    p.type = ParamDesc::Type::Bool;
    p.def = def;
    p.min = 0;
    p.max = 1;
    p.step = 1;
    return p;
}

ParamDesc pColor(const QString& id, const QString& label, const QColor& def)
{
    ParamDesc p;
    p.id = id;
    p.label = label;
    p.type = ParamDesc::Type::Color;
    p.def = def;
    return p;
}

ParamDesc pGradient(const QString& id, const QString& label, const ColorGradient& def)
{
    ParamDesc p;
    p.id = id;
    p.label = label;
    p.type = ParamDesc::Type::Gradient;
    p.def = QVariant::fromValue(def);
    return p;
}

ParamDesc pChoice(const QString& id, const QString& label, const QStringList& choices, int def)
{
    ParamDesc p;
    p.id = id;
    p.label = label;
    p.type = ParamDesc::Type::Choice;
    p.def = def;
    p.min = 0;
    p.max = std::max(0, int(choices.size()) - 1);
    p.step = 1;
    p.choices = choices;
    return p;
}

ParamDesc pAngle(double def, const QString& id, const QString& label)
{
    ParamDesc p;
    p.id = id;
    p.label = label;
    p.type = ParamDesc::Type::Angle;
    p.def = def;
    p.min = 0;
    p.max = 360;
    p.step = 1;
    p.suffix = "°";
    return p;
}

ParamDesc pCycles(int def, int min, int max)
{
    return pInt("cycles", QT_TRANSLATE_NOOP("Effects", "Repeats per loop"), def, min, max, "×");
}

ParamDesc pSeed(int def)
{
    return pInt("seed", QT_TRANSLATE_NOOP("Effects", "Seed"), def, 0, 9999);
}

void addOverlayParams(QList<ParamDesc>& list, const QColor& background)
{
    list.push_back(pBool("transparent", QT_TRANSLATE_NOOP("Effects", "Transparent background"), true));
    list.push_back(pColor("background", QT_TRANSLATE_NOOP("Effects", "Background"), background));
}

ColorGradient cyclicPalette(std::initializer_list<QColor> colors)
{
    QVector<GradientStop> stops;
    const int n = int(colors.size());
    int i = 0;
    for (const QColor& c : colors)
        stops.push_back({double(i++) / n, c});
    return ColorGradient(stops);
}

ColorGradient linearPalette(std::initializer_list<QColor> colors)
{
    return ColorGradient(colors);
}

ColorGradient stopsPalette(std::initializer_list<std::pair<double, QColor>> list)
{
    QVector<GradientStop> stops;
    for (const auto& s : list)
        stops.push_back({s.first, s.second});
    return ColorGradient(stops);
}

double projectLeds(const LedLayout& layout, double angleDeg, std::vector<double>& t)
{
    const double a = angleDeg * kPi / 180.0;
    const double dx = std::cos(a), dy = std::sin(a);
    const int n = layout.count();
    t.resize(size_t(n));
    double lo = 1e9, hi = -1e9;
    for (int i = 0; i < n; ++i) {
        const QPointF p = layout.led(i).pos;
        const double v = p.x() * dx + p.y() * dy;
        t[size_t(i)] = v;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    const double span = std::max(hi - lo, 1e-9);
    for (double& v : t)
        v = (v - lo) / span;
    return span;
}

double halfWidthAt(const LedLayout& layout, double y)
{
    const int rows = layout.rowCount();
    auto rowY = [&](int r) { return layout.led(layout.rowStart(r)).pos.y(); };
    auto rowHalf = [&](int r) { return std::abs(layout.led(layout.rowStart(r)).pos.x()); };
    if (y <= rowY(0))
        return rowHalf(0);
    for (int r = 0; r + 1 < rows; ++r) {
        const double y0 = rowY(r), y1 = rowY(r + 1);
        if (y <= y1) {
            const double t = (y - y0) / std::max(y1 - y0, 1e-9);
            return lerp(rowHalf(r), rowHalf(r + 1), t);
        }
    }
    return rowHalf(rows - 1);
}

double maxRadius(const LedLayout& layout)
{
    const QPointF c = layout.centroid();
    double r = 0;
    for (const Led& l : layout.leds())
        r = std::max(r, std::hypot(l.pos.x() - c.x(), l.pos.y() - c.y()));
    return r;
}

namespace {

std::vector<QPointF> chaikinClosed(std::vector<QPointF> p, int iterations)
{
    for (int it = 0; it < iterations; ++it) {
        std::vector<QPointF> q;
        q.reserve(p.size() * 2);
        for (size_t i = 0; i < p.size(); ++i) {
            const QPointF a = p[i], b = p[(i + 1) % p.size()];
            q.push_back(a * 0.75 + b * 0.25);
            q.push_back(a * 0.25 + b * 0.75);
        }
        p = std::move(q);
    }
    return p;
}

LoopPath resampleClosed(const std::vector<QPointF>& poly, const LedLayout& layout, int samples)
{
    LoopPath path;
    const size_t n = poly.size();
    std::vector<double> cum(n + 1, 0.0);
    for (size_t i = 0; i < n; ++i) {
        const QPointF d = poly[(i + 1) % n] - poly[i];
        cum[i + 1] = cum[i] + std::hypot(d.x(), d.y());
    }
    path.length = cum[n];
    path.pts.reserve(size_t(samples));
    size_t seg = 0;
    for (int k = 0; k < samples; ++k) {
        const double s = path.length * k / samples;
        while (seg + 1 < n && cum[seg + 1] < s)
            ++seg;
        const double segLen = std::max(cum[seg + 1] - cum[seg], 1e-9);
        const double t = (s - cum[seg]) / segLen;
        const QPointF a = poly[seg], b = poly[(seg + 1) % n];
        path.pts.push_back(a + (b - a) * t);
    }
    const int leds = layout.count();
    path.ledS.resize(size_t(leds));
    path.ledDist.resize(size_t(leds));
    for (int i = 0; i < leds; ++i) {
        const QPointF p = layout.led(i).pos;
        double best = 1e18;
        int bestK = 0;
        for (int k = 0; k < samples; ++k) {
            const QPointF d = path.pts[size_t(k)] - p;
            const double dd = d.x() * d.x() + d.y() * d.y();
            if (dd < best) {
                best = dd;
                bestK = k;
            }
        }
        path.ledS[size_t(i)] = double(bestK) / samples;
        path.ledDist[size_t(i)] = std::sqrt(best);
    }
    return path;
}

std::vector<QPointF> perimeterPolygon(const LedLayout& layout)
{
    std::vector<QPointF> poly;
    const int rows = layout.rowCount();
    for (int c = 0; c < layout.rowLength(0); ++c)
        poly.push_back(layout.led(layout.rowStart(0) + c).pos);
    for (int r = 1; r < rows - 1; ++r)
        poly.push_back(layout.led(layout.rowStart(r) + layout.rowLength(r) - 1).pos);
    for (int c = layout.rowLength(rows - 1) - 1; c >= 0; --c)
        poly.push_back(layout.led(layout.rowStart(rows - 1) + c).pos);
    for (int r = rows - 2; r >= 1; --r)
        poly.push_back(layout.led(layout.rowStart(r)).pos);
    return poly;
}

LoopPath buildPerimeterPath(const LedLayout& layout)
{
    return resampleClosed(chaikinClosed(perimeterPolygon(layout), 2), layout, 720);
}

LoopPath buildInsetPath(const LedLayout& layout, double inset)
{
    // A scaled perimeter rather than an ellipse, because it follows the dock's wedge shape.
    const QPointF c = layout.centroid();
    std::vector<QPointF> poly = chaikinClosed(perimeterPolygon(layout), 3);
    double meanR = 0;
    for (const QPointF& p : poly)
        meanR += std::hypot(p.x() - c.x(), p.y() - c.y());
    meanR /= double(poly.size());
    const double k = std::clamp((meanR - inset) / meanR, 0.1, 1.0);
    for (QPointF& p : poly)
        p = c + (p - c) * k;
    return resampleClosed(poly, layout, 720);
}

} // namespace

// Only cd2() is cached: it is the one layout that never changes, and static init is thread-safe.
LoopPath perimeterPath(const LedLayout& layout)
{
    if (&layout == &LedLayout::cd2()) {
        static const LoopPath cached = buildPerimeterPath(layout);
        return cached;
    }
    return buildPerimeterPath(layout);
}

LoopPath innerPath(const LedLayout& layout)
{
    if (&layout == &LedLayout::cd2()) {
        static const LoopPath cached = buildInsetPath(layout, kInnerPathInset);
        return cached;
    }
    return buildInsetPath(layout, kInnerPathInset);
}

CyclicPalette::CyclicPalette(const ColorGradient& g)
{
    const auto& st = g.stops();
    for (const GradientStop& s : st) {
        m_pos.push_back(s.pos);
        m_lab.push_back(color::toOklab(color::fromQColor(s.color)));
    }
    const int n = int(m_pos.size());
    if (n < 2)
        return;
    const double first = m_pos.front(), last = m_pos.back();
    const double wrap = 1.0 - last + first;
    if (wrap < 1e-3) {
        if (st.front().color == st.back().color) {
            // Already a closed loop (A ... A): drop the duplicate end.
            m_pos.pop_back();
            m_lab.pop_back();
        } else {
            // An open 0..1 gradient: give the wrap its own segment.
            const double span = std::max(last - first, 1e-9);
            for (double& p : m_pos)
                p = first + (p - first) / span * double(n - 1) / n;
        }
    }
}

Rgb CyclicPalette::sample(double u, double hold) const
{
    const int n = int(m_pos.size());
    if (n == 0)
        return white();
    if (n == 1)
        return color::fromOklab(m_lab[0]);
    const double first = m_pos.front();
    u = first + fract(u - first);
    int i = n - 1;
    for (int k = 0; k + 1 < n; ++k) {
        if (u < m_pos[size_t(k) + 1]) {
            i = k;
            break;
        }
    }
    const int j = (i + 1) % n;
    const double a = m_pos[size_t(i)];
    const double b = j == 0 ? m_pos[0] + 1.0 : m_pos[size_t(j)];
    double f = b - a > 1e-9 ? (u - a) / (b - a) : 0.0;
    f = clamp01(f);
    if (hold > 1e-6) {
        const double h = std::min(hold, 0.98) * 0.5;
        f = smooth(h, 1.0 - h, f);
    }
    const Lab& la = m_lab[size_t(i)];
    const Lab& lb = m_lab[size_t(j)];
    const float t = float(f);
    return color::fromOklab({la.L + (lb.L - la.L) * t, la.a + (lb.a - la.a) * t,
                             la.b + (lb.b - la.b) * t});
}

namespace {

inline uint32_t mix32(uint32_t h)
{
    h ^= h >> 16;
    h *= 0x7feb352dU;
    h ^= h >> 15;
    h *= 0x846ca68bU;
    h ^= h >> 16;
    return h;
}

inline double lattice4(uint32_t seed, int x, int y, int z, int w)
{
    uint32_t h = mix32(seed + 0x51ED270BU);
    h = mix32(h ^ uint32_t(x) * 0x8DA6B343U);
    h = mix32(h ^ uint32_t(y) * 0xD8163841U);
    h = mix32(h ^ uint32_t(z) * 0xCB1AB31FU);
    h = mix32(h ^ uint32_t(w) * 0x165667B1U);
    return h / 4294967296.0;
}

inline double quintic(double t)
{
    return t * t * t * (t * (t * 6 - 15) + 10);
}

} // namespace

double valueNoise4(double x, double y, double z, double w, uint32_t seed, int periodY)
{
    const int xi = int(std::floor(x)), yi = int(std::floor(y)), zi = int(std::floor(z)),
              wi = int(std::floor(w));
    const double fx_ = quintic(x - xi), fy = quintic(y - yi), fz = quintic(z - zi),
                 fw = quintic(w - wi);
    auto wrapY = [&](int v) { return periodY > 0 ? imod(v, periodY) : v; };
    double acc[2];
    for (int dw = 0; dw < 2; ++dw) {
        double zs[2];
        for (int dz = 0; dz < 2; ++dz) {
            double ys[2];
            for (int dy = 0; dy < 2; ++dy) {
                const int yy = wrapY(yi + dy);
                const double a = lattice4(seed, xi, yy, zi + dz, wi + dw);
                const double b = lattice4(seed, xi + 1, yy, zi + dz, wi + dw);
                ys[dy] = a + (b - a) * fx_;
            }
            zs[dz] = ys[0] + (ys[1] - ys[0]) * fy;
        }
        acc[dw] = zs[0] + (zs[1] - zs[0]) * fz;
    }
    return acc[0] + (acc[1] - acc[0]) * fw;
}

double scrollNoise(double x, double y, double phase, uint32_t seed, int periodY, int scroll,
                   double morph)
{
    const double a = kTau * phase;
    const double yy = y + phase * double(scroll) * double(periodY);
    return valueNoise4(x, yy, morph * std::cos(a) + 5.31, morph * std::sin(a) + 9.73, seed,
                       periodY);
}

double loopFbm(double x, double y, double phase, uint32_t seed, double timeRadius)
{
    const double n1 = fx::loopNoise(x, y, phase, seed, timeRadius);
    const double n2 = fx::loopNoise(x * 2.03 + 3.1, y * 2.03 + 1.7, phase, seed + 7919U, timeRadius);
    return n1 * 0.7 + n2 * 0.3;
}

} // namespace fg::fxi
