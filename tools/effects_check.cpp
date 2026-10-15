#include "core/Color.h"
#include "core/Effect.h"
#include "core/LedLayout.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace fg;

namespace {

const char kUsage[] =
    "Renders every built-in effect over one loop, checks it is finite, in range, deterministic,\n"
    "seamless and free of harsh jumps, and writes a preview PNG per effect: a filmstrip plus a\n"
    "kymograph (time across, LED index down, two loops side by side).\n"
    "\n"
    "usage:\n"
    "  effects_check <out-dir> [id1,id2,...|all] [param=value ...]\n"
    "      param=value applies to every selected effect with that parameter (number, true/false,\n"
    "      #rrggbb); the PNG name then carries the overrides, e.g. stars__transparent=false.png\n"
    "  effects_check --dump <id> <led>\n"
    "      prints one LED's colour and per-frame step over the loop\n";

constexpr int kFrames = 240;
constexpr int kStripFrames = 10;
constexpr int kStripCols = 5;
constexpr double kPx = 13.0;   // preview pixels per layout unit

// Composited over black like the scene's Normal blend, as 8-bit sRGB.
void toDisplay(const Frame& f, std::vector<float>& out)
{
    out.resize(f.size() * 3);
    for (size_t i = 0; i < f.size(); ++i) {
        const float a = std::clamp(f[i].a, 0.f, 1.f);
        out[i * 3 + 0] = color::linearToSrgb(f[i].r * a) * 255.f;
        out[i * 3 + 1] = color::linearToSrgb(f[i].g * a) * 255.f;
        out[i * 3 + 2] = color::linearToSrgb(f[i].b * a) * 255.f;
    }
}

void toLab(const Frame& f, std::vector<float>& out)
{
    out.resize(f.size() * 3);
    for (size_t i = 0; i < f.size(); ++i) {
        const float a = std::clamp(f[i].a, 0.f, 1.f);
        const Lab l = color::toOklab(Rgb{std::max(0.f, f[i].r * a), std::max(0.f, f[i].g * a),
                                         std::max(0.f, f[i].b * a)});
        out[i * 3 + 0] = l.L * 100.f;
        out[i * 3 + 1] = l.a * 100.f;
        out[i * 3 + 2] = l.b * 100.f;
    }
}

float labDist(const std::vector<float>& a, const std::vector<float>& b, size_t led)
{
    const float dl = a[led * 3] - b[led * 3], da = a[led * 3 + 1] - b[led * 3 + 1],
                db = a[led * 3 + 2] - b[led * 3 + 2];
    return std::sqrt(dl * dl + da * da + db * db);
}

float frameDiff(const std::vector<float>& a, const std::vector<float>& b, int* led = nullptr)
{
    float best = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        const float d = std::abs(a[i] - b[i]);
        if (d > best) {
            best = d;
            if (led)
                *led = int(i / 3);
        }
    }
    return best;
}

struct Validity {
    int nonFinite = 0, rgbOut = 0, alphaOut = 0;
    void check(const Frame& f)
    {
        for (const Rgba& c : f) {
            if (!std::isfinite(c.r) || !std::isfinite(c.g) || !std::isfinite(c.b) || !std::isfinite(c.a)) {
                ++nonFinite;
                continue;
            }
            if (c.r < 0 || c.g < 0 || c.b < 0 || c.r > 1.001f || c.g > 1.001f || c.b > 1.001f)
                ++rgbOut;
            if (c.a < 0 || c.a > 1)
                ++alphaOut;
        }
    }
    bool ok() const { return nonFinite == 0 && rgbOut == 0 && alphaOut == 0; }
};

struct LoopStats {
    Validity valid;
    float maxStep = 0, p99Step = 0, meanStep = 0;
    int maxStepFrame = -1, maxStepLed = -1;
    float seamStep = 0;    // last frame -> frame 0
    float exactSeam = 0;   // render(1 - 1e-7) vs render(0)
    float maxDE = 0, p99DE = 0, seamDE = 0;   // OKLab distance x100
    int maxDEFrame = -1, maxDELed = -1;
    bool deterministic = true;
    bool staticOk = true;
    double usPerFrame = 0;
};

LoopStats runLoop(const Effect& e, const LedLayout& layout, int frames,
                  std::vector<Frame>* keep = nullptr)
{
    LoopStats s;
    const int n = layout.count();
    RenderContext ctx;
    ctx.layout = &layout;
    ctx.loopSeconds = 4.0;
    std::vector<std::vector<float>> disp(static_cast<size_t>(frames));
    std::vector<std::vector<float>> labs(static_cast<size_t>(frames));
    Frame f(static_cast<size_t>(n));
    std::vector<Frame> rendered;
    QElapsedTimer timer;
    timer.start();
    for (int k = 0; k < frames; ++k) {
        ctx.phase = double(k) / frames;
        std::fill(f.begin(), f.end(), Rgba());
        e.render(ctx, f);
        rendered.push_back(f);
    }
    s.usPerFrame = double(timer.nsecsElapsed()) / 1000.0 / frames;
    for (int k = 0; k < frames; ++k) {
        s.valid.check(rendered[size_t(k)]);
        toDisplay(rendered[size_t(k)], disp[size_t(k)]);
        toLab(rendered[size_t(k)], labs[size_t(k)]);
    }
    if (keep)
        *keep = rendered;

    std::vector<float> des;
    des.reserve(size_t(frames) * size_t(n));
    for (int k = 0; k < frames; ++k) {
        const auto& a = labs[size_t(k)];
        const auto& b = labs[size_t((k + 1) % frames)];
        for (int i = 0; i < n; ++i) {
            const float d = labDist(a, b, size_t(i));
            if (k + 1 == frames) {
                s.seamDE = std::max(s.seamDE, d);
                continue;
            }
            des.push_back(d);
            if (d > s.maxDE) {
                s.maxDE = d;
                s.maxDEFrame = k;
                s.maxDELed = i;
            }
        }
    }
    if (!des.empty()) {
        std::nth_element(des.begin(), des.begin() + qsizetype(des.size() * 0.99), des.end());
        s.p99DE = des[size_t(des.size() * 0.99)];
    }

    std::vector<float> steps;
    steps.reserve(size_t(frames) * size_t(n));
    for (int k = 0; k + 1 < frames; ++k) {
        const auto& a = disp[size_t(k)];
        const auto& b = disp[size_t(k) + 1];
        for (int i = 0; i < n; ++i) {
            float d = 0;
            for (int c = 0; c < 3; ++c)
                d = std::max(d, std::abs(a[size_t(i) * 3 + c] - b[size_t(i) * 3 + c]));
            steps.push_back(d);
            s.meanStep += d;
            if (d > s.maxStep) {
                s.maxStep = d;
                s.maxStepFrame = k;
                s.maxStepLed = i;
            }
        }
    }
    if (!steps.empty()) {
        s.meanStep /= float(steps.size());
        std::nth_element(steps.begin(), steps.begin() + qsizetype(steps.size() * 0.99), steps.end());
        s.p99Step = steps[size_t(steps.size() * 0.99)];
    }
    s.seamStep = frameDiff(disp.back(), disp.front());

    Frame f0(static_cast<size_t>(n)), f1(static_cast<size_t>(n)), f0b(static_cast<size_t>(n));
    ctx.phase = 0;
    e.render(ctx, f0);
    e.render(ctx, f0b);
    ctx.phase = 1.0 - 1e-7;
    e.render(ctx, f1);
    std::vector<float> d0, d1, d0b;
    toDisplay(f0, d0);
    toDisplay(f1, d1);
    toDisplay(f0b, d0b);
    s.exactSeam = frameDiff(d0, d1);
    s.deterministic = frameDiff(d0, d0b) == 0.f;
    if (!e.isAnimated())
        for (const auto& d : disp)
            if (frameDiff(d, disp.front()) > 0.f)
                s.staticOk = false;
    return s;
}

struct Splat {
    int pixel;
    int led;
    float w;
};

struct Preview {
    int tileW = 0, tileH = 0;
    QRectF view;                // layout units
    QImage background;
    std::vector<Splat> splats;

    explicit Preview(const LedLayout& layout)
    {
        view = layout.outline().boundingRect().adjusted(-0.6, -0.6, 0.6, 0.6);
        tileW = int(std::ceil(view.width() * kPx));
        tileH = int(std::ceil(view.height() * kPx));
        background = QImage(tileW, tileH, QImage::Format_ARGB32);
        background.fill(QColor(9, 9, 13));
        {
            QPainter p(&background);
            p.setRenderHint(QPainter::Antialiasing);
            p.scale(kPx, kPx);
            p.translate(-view.left(), -view.top());
            p.setPen(QPen(QColor(46, 46, 58), 1.4 / kPx));
            p.setBrush(QColor(17, 17, 23));
            p.drawPath(layout.outline());
        }
        for (int py = 0; py < tileH; ++py) {
            for (int px = 0; px < tileW; ++px) {
                const double x = view.left() + (px + 0.5) / kPx;
                const double y = view.top() + (py + 0.5) / kPx;
                for (const Led& l : layout.leds()) {
                    const double d = std::hypot(l.pos.x() - x, l.pos.y() - y);
                    if (d > 1.7)
                        continue;
                    // Anti-aliased LED die plus a soft diffuser glow.
                    const double core = fx::smooth(0.40, 0.30, d);
                    const double glow = 0.20 * std::exp(-d * d / (2 * 0.5 * 0.5));
                    splats.push_back({py * tileW + px, l.index, float(core + glow)});
                }
            }
        }
    }

    QImage tile(const Frame& f) const
    {
        QImage img = background.copy();
        std::vector<float> acc(size_t(tileW) * size_t(tileH) * 3, 0.f);
        std::vector<char> touched(size_t(tileW) * size_t(tileH), 0);
        for (const Splat& s : splats) {
            const Rgba& c = f[size_t(s.led)];
            const float a = std::clamp(c.a, 0.f, 1.f) * s.w;
            float* px = &acc[size_t(s.pixel) * 3];
            px[0] += c.r * a;
            px[1] += c.g * a;
            px[2] += c.b * a;
            touched[size_t(s.pixel)] = 1;
        }
        for (int i = 0; i < tileW * tileH; ++i) {
            if (!touched[size_t(i)])
                continue;
            QRgb* line = reinterpret_cast<QRgb*>(img.bits()) + i;
            const float* px = &acc[size_t(i) * 3];
            const float r = color::srgbToLinear(qRed(*line) / 255.f) + px[0];
            const float g = color::srgbToLinear(qGreen(*line) / 255.f) + px[1];
            const float b = color::srgbToLinear(qBlue(*line) / 255.f) + px[2];
            *line = qRgb(int(color::linearToSrgb(r) * 255.f + 0.5f), int(color::linearToSrgb(g) * 255.f + 0.5f),
                         int(color::linearToSrgb(b) * 255.f + 0.5f));
        }
        return img;
    }
};

QImage previewImage(const Effect& e, const LedLayout& layout, const Preview& pv,
                    const std::vector<Frame>& frames, const LoopStats& st)
{
    const int header = 34;
    const int rows = (kStripFrames + kStripCols - 1) / kStripCols;
    const int stripW = pv.tileW * kStripCols;
    const int kymoRowPx = 2;
    const int kymoH = layout.count() * kymoRowPx;
    const int kymoTop = header + rows * pv.tileH + 22;
    QImage img(stripW, kymoTop + kymoH + 6, QImage::Format_ARGB32);
    img.fill(QColor(20, 20, 26));

    QPainter p(&img);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont font("Segoe UI", 11);
    font.setBold(true);
    p.setFont(font);
    p.setPen(QColor(235, 235, 240));
    p.drawText(QRect(10, 0, stripW - 20, header), Qt::AlignVCenter | Qt::AlignLeft,
               QString("%1  ·  %2  ·  %3").arg(e.name(), e.typeId(), e.category()));
    QFont small("Segoe UI", 9);
    p.setFont(small);
    p.setPen(QColor(170, 170, 185));
    p.drawText(QRect(10, 0, stripW - 20, header), Qt::AlignVCenter | Qt::AlignRight,
               QString("ΔE max %1  p99 %2  seam %3   |   sRGB max %4  seam %5  exact seam %6")
                   .arg(st.maxDE, 0, 'f', 1)
                   .arg(st.p99DE, 0, 'f', 1)
                   .arg(st.seamDE, 0, 'f', 1)
                   .arg(st.maxStep, 0, 'f', 1)
                   .arg(st.seamStep, 0, 'f', 1)
                   .arg(st.exactSeam, 0, 'f', 2));

    RenderContext ctx;
    ctx.layout = &layout;
    Frame f(size_t(layout.count()));
    for (int k = 0; k < kStripFrames; ++k) {
        ctx.phase = double(k) / kStripFrames;
        std::fill(f.begin(), f.end(), Rgba());
        e.render(ctx, f);
        const int x = (k % kStripCols) * pv.tileW;
        const int y = header + (k / kStripCols) * pv.tileH;
        p.drawImage(x, y, pv.tile(f));
        p.setPen(QColor(120, 120, 135));
        p.drawText(QRect(x + 6, y + 4, 80, 16), Qt::AlignLeft | Qt::AlignTop,
                   QString("φ %1").arg(ctx.phase, 0, 'f', 2));
    }

    p.setPen(QColor(150, 150, 165));
    p.drawText(QRect(6, kymoTop - 20, stripW - 12, 18), Qt::AlignLeft | Qt::AlignVCenter,
               "time → (2 loops, seam in the middle), LEDs ↓ by index");
    p.fillRect(stripW / 2 - 1, kymoTop - 4, 2, 4, QColor(255, 80, 80));
    for (int x = 0; x < stripW; ++x) {
        const int k = int(double(x) / stripW * 2 * kFrames) % kFrames;
        const Frame& fr = frames[size_t(k)];
        for (int i = 0; i < layout.count(); ++i) {
            const Rgba& c = fr[size_t(i)];
            const float a = std::clamp(c.a, 0.f, 1.f);
            const QColor q = color::toQColor(Rgb{c.r * a, c.g * a, c.b * a});
            for (int r = 0; r < kymoRowPx; ++r)
                img.setPixelColor(x, kymoTop + i * kymoRowPx + r, q);
        }
    }
    return img;
}

QStringList sweep(const QString& typeId, const LedLayout& layout)
{
    QStringList failures;
    std::unique_ptr<Effect> probe = EffectRegistry::instance().create(typeId);
    struct Variant {
        QString label;
        QString id;
        QVariant value;
    };
    std::vector<Variant> variants;
    for (const ParamDesc& pd : probe->params()) {
        switch (pd.type) {
        case ParamDesc::Type::Float:
        case ParamDesc::Type::Angle:
            variants.push_back({pd.id + "=min", pd.id, pd.min});
            variants.push_back({pd.id + "=max", pd.id, pd.max});
            break;
        case ParamDesc::Type::Int:
            variants.push_back({pd.id + "=min", pd.id, int(pd.min)});
            variants.push_back({pd.id + "=max", pd.id, int(pd.max)});
            break;
        case ParamDesc::Type::Bool:
            variants.push_back({pd.id + "=!def", pd.id, !pd.def.toBool()});
            break;
        case ParamDesc::Type::Choice:
            for (int c = 0; c < pd.choices.size(); ++c)
                variants.push_back({pd.id + "=" + QString::number(c), pd.id, c});
            break;
        case ParamDesc::Type::Gradient:
            variants.push_back({pd.id + "=single", pd.id,
                                QVariant::fromValue(ColorGradient({QColor(255, 120, 0)}))});
            variants.push_back({pd.id + "=open2", pd.id,
                                QVariant::fromValue(ColorGradient({QColor(0, 0, 255), QColor(255, 0, 0)}))});
            break;
        case ParamDesc::Type::Color:
            variants.push_back({pd.id + "=white", pd.id, QColor(255, 255, 255)});
            break;
        }
    }
    for (const Variant& v : variants) {
        std::unique_ptr<Effect> e = EffectRegistry::instance().create(typeId);
        e->setValue(v.id, v.value);
        const LoopStats s = runLoop(*e, layout, 96);
        QStringList why;
        if (!s.valid.ok())
            why << QString("invalid(nan %1, rgb %2, a %3)")
                       .arg(s.valid.nonFinite)
                       .arg(s.valid.rgbOut)
                       .arg(s.valid.alphaOut);
        if (s.exactSeam > 1.0f)
            why << QString("exact seam %1").arg(s.exactSeam, 0, 'f', 2);
        if (s.seamDE > s.maxDE * 1.05f + 0.3f)
            why << QString("seam ΔE %1 > max step ΔE %2").arg(s.seamDE, 0, 'f', 1).arg(s.maxDE, 0, 'f', 1);
        if (!s.deterministic)
            why << "nondeterministic";
        if (!s.staticOk)
            why << "static but changes";
        if (!why.isEmpty())
            failures << v.label + ": " + why.join(", ");
    }
    return failures;
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() > 1 && (args[1] == "--help" || args[1] == "-h")) {
        std::printf("%s", kUsage);
        return 0;
    }
    const QString outDir = args.size() > 1 ? args[1] : QString("fx_out");
    QStringList only = args.size() > 2 ? args[2].split(',', Qt::SkipEmptyParts) : QStringList();
    if (only == QStringList{"all"})
        only.clear();
    QList<QPair<QString, QString>> overrides;
    for (int a = 3; a < args.size(); ++a) {
        const qsizetype eq = args[a].indexOf('=');
        if (eq > 0)
            overrides.push_back({args[a].left(eq), args[a].mid(eq + 1)});
    }

    registerBuiltinEffects();
    const LedLayout& layout = LedLayout::cd2();

    if (args.size() > 3 && args[1] == "--dump") {
        std::unique_ptr<Effect> e = EffectRegistry::instance().create(args[2]);
        const int led = args[3].toInt();
        if (!e || led < 0 || led >= layout.count())
            return 2;
        RenderContext ctx;
        ctx.layout = &layout;
        Frame f(static_cast<size_t>(layout.count()));
        std::vector<float> d, prev;
        for (int k = 0; k < kFrames; ++k) {
            ctx.phase = double(k) / kFrames;
            e->render(ctx, f);
            toDisplay(f, d);
            const float step = prev.empty() ? 0.f : std::max({std::abs(d[size_t(led) * 3] - prev[size_t(led) * 3]),
                                                              std::abs(d[size_t(led) * 3 + 1] - prev[size_t(led) * 3 + 1]),
                                                              std::abs(d[size_t(led) * 3 + 2] - prev[size_t(led) * 3 + 2])});
            const Rgba& c = f[size_t(led)];
            std::printf("%3d  srgb %6.1f %6.1f %6.1f  lin %.4f %.4f %.4f a %.3f  step %5.1f\n", k,
                        d[size_t(led) * 3], d[size_t(led) * 3 + 1], d[size_t(led) * 3 + 2], c.r, c.g, c.b, c.a,
                        step);
            prev = d;
        }
        return 0;
    }

    QDir().mkpath(outDir);
    const Preview preview(layout);

    std::printf("Per-LED frame-to-frame change over %d frames/loop. dE = OKLab distance x100 of the\n"
                "colour over black (perceptual); rgb = max 8-bit sRGB channel. seam = last->first frame,\n"
                "exact = render(1-1e-7) vs render(0) in 8-bit sRGB.\n\n",
                kFrames);
    std::printf("%-15s %5s %5s %5s | %5s %5s %5s | %6s %7s %5s  %s\n", "id", "dEmax", "dEp99", "dEsm",
                "rgbmx", "rgb99", "rgbsm", "exact", "us/frm", "valid", "notes");
    int problems = 0;
    for (const EffectRegistry::Entry& entry : EffectRegistry::instance().entries()) {
        if (!only.isEmpty() && !only.contains(entry.typeId))
            continue;
        std::unique_ptr<Effect> e = entry.make();
        QString suffix;
        for (const auto& [key, text] : overrides) {
            for (const ParamDesc& pd : e->params()) {
                if (pd.id != key)
                    continue;
                QVariant v;
                switch (pd.type) {
                case ParamDesc::Type::Bool:
                    v = text == "true" || text == "1";
                    break;
                case ParamDesc::Type::Int:
                case ParamDesc::Type::Choice:
                    v = text.toInt();
                    break;
                case ParamDesc::Type::Color:
                    v = QColor(text);
                    break;
                case ParamDesc::Type::Gradient:
                    continue;   // gradients can't be given on the command line
                default:
                    v = text.toDouble();
                    break;
                }
                e->setValue(key, v);
                suffix += "__" + key + "=" + text;
            }
        }
        if (!overrides.isEmpty() && suffix.isEmpty())
            continue;
        std::vector<Frame> frames;
        const LoopStats s = runLoop(*e, layout, kFrames, &frames);

        QStringList notes;
        if (s.seamDE > s.maxDE * 1.05f + 0.3f)
            notes << "SEAM";
        if (s.exactSeam > 1.0f)
            notes << "EXACT-SEAM";
        if (!s.deterministic)
            notes << "NONDETERMINISTIC";
        if (!s.staticOk)
            notes << "STATIC-CHANGES";
        if (s.maxDE > 15.f)
            notes << QString("harsh step @frame %1 led %2").arg(s.maxDEFrame).arg(s.maxDELed);
        const QStringList sw = overrides.isEmpty() ? sweep(entry.typeId, layout) : QStringList();
        if (!sw.isEmpty())
            notes << QString("sweep failures: %1").arg(sw.size());
        if (!s.valid.ok() || !sw.isEmpty() || notes.contains("SEAM") || notes.contains("EXACT-SEAM")
            || !s.deterministic || !s.staticOk)
            ++problems;

        std::printf("%-15s %5.1f %5.1f %5.1f | %5.1f %5.1f %5.1f | %6.2f %7.1f %5s  %s\n",
                    (entry.typeId + suffix).toUtf8().constData(), s.maxDE, s.p99DE, s.seamDE, s.maxStep, s.p99Step,
                    s.seamStep, s.exactSeam, s.usPerFrame, s.valid.ok() ? "ok" : "FAIL",
                    notes.join("; ").toUtf8().constData());
        for (const QString& f : sw)
            std::printf("    sweep: %s\n", f.toUtf8().constData());

        const QImage img = previewImage(*e, layout, preview, frames, s);
        img.save(QDir(outDir).filePath(entry.typeId + suffix + ".png"));
    }
    std::printf("\n%d effect(s) with problems\n", problems);
    return problems == 0 ? 0 : 1;
}
