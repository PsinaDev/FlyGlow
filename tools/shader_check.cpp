#include "core/LedLayout.h"
#include "shader/ShaderEffect.h"
#include "shader/ShaderRenderer.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <cstdio>

using namespace fg;

namespace {

const char kUsage[] =
    "Compiles a Shadertoy-style shader the way the Shader layer does, prints the driver's messages and\n"
    "render timing, and writes the raw GPU frame plus the LED result at a few points of the loop.\n"
    "\n"
    "usage:\n"
    "  shader_check <shader.glsl> <out-dir> [loop-seconds]\n";

constexpr double kPx = 14.0;   // preview pixels per layout unit

QImage ledPreview(const Frame& frame)
{
    const LedLayout& L = LedLayout::cd2();
    const QRectF box = L.bounds().adjusted(-1, -1, 1, 1);
    QImage img((box.size() * kPx).toSize(), QImage::Format_RGB32);
    img.fill(QColor(0x14, 0x16, 0x1c));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    for (const Led& led : L.leds()) {
        const Rgba& c = frame[size_t(led.index)];
        p.setBrush(color::toQColor(Rgb{c.r * c.a, c.g * c.a, c.b * c.a}));
        p.drawEllipse((led.pos - box.topLeft()) * kPx, kPx * 0.42, kPx * 0.42);
    }
    return img;
}

} // namespace

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    if (argc < 3) {
        std::fputs(kUsage, stderr);
        return 2;
    }
    QFile file(QString::fromLocal8Bit(argv[1]));
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    const QString source = QString::fromUtf8(file.readAll());
    const QDir out(QString::fromLocal8Bit(argv[2]));
    QDir().mkpath(out.path());
    const double loop = argc > 3 ? QString::fromLocal8Bit(argv[3]).toDouble() : 4.0;

    ShaderRenderer renderer;
    if (!renderer.isAvailable()) {
        std::fputs("no OpenGL 3.3 context\n", stderr);
        return 1;
    }
    QElapsedTimer clock;
    clock.start();
    const ShaderCompileResult result = renderer.compile(source);
    std::printf("compile: %s in %lld ms\n", result.ok ? "ok" : "FAILED", static_cast<long long>(clock.elapsed()));
    for (const ShaderIssue& issue : result.issues)
        std::printf("  line %d: %s\n", issue.line, qPrintable(issue.text));
    if (!result.ok)
        return 1;

    ShaderEffect effect;
    effect.setSource(source);
    Frame frame(size_t(LedLayout::cd2().count()));
    RenderContext ctx;
    ctx.loopSeconds = loop;
    const double phases[] = {0.0, 0.25, 0.5, 0.9, 0.999};
    for (double phase : phases) {
        ctx.phase = phase;
        ShaderTime t;
        t.time = phase * loop;
        t.phase = phase;
        t.loop = loop;
        const QString tag = QString::number(int(phase * 1000)).rightJustified(4, QLatin1Char('0'));
        renderer.render(source, t).save(out.filePath(QStringLiteral("gpu_%1.png").arg(tag)));
        effect.render(ctx, frame);
        ledPreview(frame).save(out.filePath(QStringLiteral("leds_%1.png").arg(tag)));
    }

    constexpr int kTimed = 50;
    clock.restart();
    for (int k = 0; k < kTimed; ++k) {
        ctx.phase = double(k) / kTimed;
        effect.render(ctx, frame);
    }
    std::printf("render + sample: %.2f ms per frame\n", double(clock.nsecsElapsed()) / 1e6 / kTimed);
    return 0;
}
