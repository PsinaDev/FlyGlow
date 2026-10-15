#include "core/Color.h"
#include "core/LedLayout.h"
#include "core/MediaEffect.h"
#include "media/LedSampler.h"
#include "media/MediaDecoder.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFuture>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QPainter>
#include <QPainterPath>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

using namespace fg;

namespace {

constexpr int kTiles = 10;
constexpr int kTileW = 200;
constexpr int kSourceH = 150;
constexpr int kDockH = 190;

float maxDiff(const Frame& a, const Frame& b)
{
    float best = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        best = std::max(best, std::abs(color::linearToSrgb(a[i].r) - color::linearToSrgb(b[i].r)) * 255.f);
        best = std::max(best, std::abs(color::linearToSrgb(a[i].g) - color::linearToSrgb(b[i].g)) * 255.f);
        best = std::max(best, std::abs(color::linearToSrgb(a[i].b) - color::linearToSrgb(b[i].b)) * 255.f);
    }
    return best;
}

void drawSource(QPainter& p, const QRectF& area, const QImage& frame, const media::Framing& framing)
{
    const LedLayout& layout = LedLayout::cd2();
    const QSize view = media::viewSize(frame.size(), framing.rotation);
    const double s = std::min(area.width() / view.width(), area.height() / view.height());
    QTransform viewToTile;
    viewToTile.translate(area.center().x() - view.width() * s / 2, area.center().y() - view.height() * s / 2);
    viewToTile.scale(s, s);

    p.save();
    p.setClipRect(area);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setTransform(media::sourceToView(frame.size(), framing) * viewToTile);
    p.drawImage(0, 0, frame);
    p.resetTransform();
    const QTransform layoutToTile = media::layoutToView(layout, frame.size(), framing) * viewToTile;
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(255, 255, 255, 220), 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawPath(layoutToTile.map(layout.outline()));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 150));
    for (const Led& led : layout.leds())
        p.drawEllipse(layoutToTile.map(led.pos), 0.8, 0.8);
    p.restore();
}

void drawDock(QPainter& p, const QRectF& area, const Frame& frame)
{
    const LedLayout& layout = LedLayout::cd2();
    const QRectF box = layout.outline().boundingRect().adjusted(-0.4, -0.4, 0.4, 0.4);
    const double s = std::min(area.width() / box.width(), area.height() / box.height());
    QTransform xf;
    xf.translate(area.center().x(), area.center().y());
    xf.scale(s, s);
    xf.translate(-box.center().x(), -box.center().y());

    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(46, 46, 58), 1.2));
    p.setBrush(QColor(17, 17, 23));
    p.drawPath(xf.map(layout.outline()));
    p.setPen(Qt::NoPen);
    for (const Led& led : layout.leds()) {
        const Rgba& c = frame[size_t(led.index)];
        p.setBrush(color::toQColor(c.rgb()));
        p.drawEllipse(xf.map(led.pos), 0.46 * s, 0.46 * s);
    }
    p.restore();
}

media::FitMode parseFit(const QString& v)
{
    if (v == "contain")
        return media::FitMode::Contain;
    if (v == "stretch")
        return media::FitMode::Stretch;
    return media::FitMode::Cover;
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 3) {
        std::fprintf(stderr,
                     "Decodes an image or video like the import dialog, samples it onto the dock's LEDs, checks\n"
                     "the resulting effect (seamless loop, JSON round trip) and writes a filmstrip PNG.\n"
                     "\n"
                     "usage: media_check <file> <out-dir> [key=value ...]\n"
                     "  max=200 start=0 end=0         frame budget and trim range in seconds (end 0 = to the end)\n"
                     "  fit=cover|contain|stretch zoom=1 panx=0 pany=0 rot=0 flip=0\n"
                     "  bright=1 sat=1\n");
        return 2;
    }
    const QString path = args[1];
    const QString outDir = args[2];
    QDir().mkpath(outDir);

    media::DecodeOptions options;
    media::Framing framing;
    media::Adjustments adjust;
    for (int a = 3; a < args.size(); ++a) {
        const QString key = args[a].section('=', 0, 0);
        const QString val = args[a].section('=', 1);
        if (key == "max")
            options.maxFrames = val.toInt();
        else if (key == "start")
            options.trimStartSeconds = val.toDouble();
        else if (key == "end")
            options.trimEndSeconds = val.toDouble();
        else if (key == "fit")
            framing.fit = parseFit(val);
        else if (key == "zoom")
            framing.zoom = val.toDouble();
        else if (key == "panx")
            framing.panX = val.toDouble();
        else if (key == "pany")
            framing.panY = val.toDouble();
        else if (key == "rot")
            framing.rotation = val.toInt();
        else if (key == "flip")
            framing.flip = val.toInt() != 0;
        else if (key == "bright")
            adjust.brightness = val.toDouble();
        else if (key == "sat")
            adjust.saturation = val.toDouble();
    }

    std::printf("file      %s\n", qPrintable(QDir::toNativeSeparators(path)));
    QElapsedTimer timer;
    timer.start();
    std::atomic<int> lastDecile{-1};
    QFuture<media::DecodedClip> future = QtConcurrent::run([&] {
        return media::decodeFile(path, options, [&](double p) {
            const int decile = p < 0 ? -1 : int(p * 10);
            if (decile > lastDecile.exchange(decile)) {
                std::printf(".");
                std::fflush(stdout);
            }
            return true;
        });
    });
    const media::DecodedClip clip = future.result();
    const qint64 decodeMs = timer.elapsed();
    std::printf("\n");
    if (!clip.ok()) {
        std::printf("FAILED    %s\n", qPrintable(clip.error));
        return 1;
    }

    double minD = 1e9, maxD = 0;
    for (double d : clip.durations) {
        minD = std::min(minD, d);
        maxD = std::max(maxD, d);
    }
    std::printf("kind      %s\n", clip.isVideo ? "video (Media Foundation)" : "image (QImageReader)");
    std::printf("source    %dx%d, %.3f s, %.3f fps\n", clip.sourceSize.width(), clip.sourceSize.height(),
                clip.sourceDuration, clip.sourceFps);
    std::printf("frames    %d (%dx%d)%s\n", int(clip.frames.size()), clip.frames[0].width(), clip.frames[0].height(),
                clip.resampled ? ", respaced to fit the frame budget" : "");
    std::printf("duration  %.3f s, frame %.4f..%.4f s\n", clip.totalSeconds(), minD, maxD);
    std::printf("first     ");
    for (size_t i = 0; i < std::min<size_t>(8, clip.durations.size()); ++i)
        std::printf("%.4f ", clip.durations[i]);
    std::printf("\n");
    std::printf("decode    %lld ms\n", decodeMs);

    const media::LedSampler sampler(LedLayout::cd2());
    timer.restart();
    std::vector<std::vector<Rgb>> sampled = sampler.sampleAll(clip.frames, framing, adjust);
    const qint64 sampleMs = timer.elapsed();
    std::printf("sample    %lld ms (%.2f ms/frame)\n", sampleMs, double(sampleMs) / clip.frames.size());

    MediaEffect effect;
    effect.setClip(sampled, clip.durations);
    std::printf("effect    %d frames, %.3f s, animated=%d\n", effect.frameCount(), effect.clipSeconds(),
                int(effect.isAnimated()));

    RenderContext ctx;
    ctx.layout = &LedLayout::cd2();
    ctx.loopSeconds = std::max(0.02, effect.clipSeconds());
    const size_t n = size_t(LedLayout::cd2().count());
    Frame f0(n), f1(n), g(n);
    ctx.phase = 0;
    effect.render(ctx, f0);
    ctx.phase = 1.0 - 1e-9;
    effect.render(ctx, f1);
    std::printf("seam      %.2f (8-bit, phase 0 vs 1-eps, smooth on)\n", maxDiff(f0, f1));

    float worstStep = 0;
    const int steps = 400;
    Frame prev(n);
    ctx.phase = 0;
    effect.render(ctx, prev);
    for (int s = 1; s <= steps; ++s) {
        ctx.phase = double(s) / steps;
        effect.render(ctx, g);
        worstStep = std::max(worstStep, maxDiff(prev, g));
        prev = g;
    }
    std::printf("step      %.1f max 8-bit change between %d evenly spaced phases\n", worstStep, steps);

    const QJsonObject json = effect.toJson();
    const QByteArray bytes = QJsonDocument(json).toJson(QJsonDocument::Compact);
    std::unique_ptr<Effect> copy = std::make_unique<MediaEffect>();
    copy->fromJson(QJsonDocument::fromJson(bytes).object());
    float roundTrip = 0;
    for (int s = 0; s < 50; ++s) {
        ctx.phase = s / 50.0;
        effect.render(ctx, g);
        copy->render(ctx, prev);
        roundTrip = std::max(roundTrip, maxDiff(g, prev));
    }
    std::printf("json      %lld bytes, round trip max diff %.3f\n", qint64(bytes.size()), roundTrip);

    const int tiles = std::min(kTiles, int(clip.frames.size()));
    QImage strip(tiles * kTileW + 8, kSourceH + kDockH + 34, QImage::Format_RGB32);
    strip.fill(QColor(9, 9, 13));
    {
        QPainter p(&strip);
        p.setPen(QColor(150, 155, 170));
        QFont font = p.font();
        font.setPixelSize(12);
        p.setFont(font);
        p.drawText(QRect(8, 4, strip.width() - 16, 18), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("%1  ·  %2 frames  ·  %3 s").arg(QFileInfo(path).fileName())
                       .arg(clip.frames.size()).arg(clip.totalSeconds(), 0, 'f', 2));
        for (int t = 0; t < tiles; ++t) {
            const int index = tiles > 1 ? int(std::lround(double(t) * (clip.frames.size() - 1) / (tiles - 1))) : 0;
            const QRectF col(4 + t * kTileW + 4, 26, kTileW - 8, kSourceH);
            drawSource(p, col, clip.frames[size_t(index)], framing);
            Frame lit(n);
            for (size_t i = 0; i < n; ++i)
                lit[i] = Rgba(effect.frame(index)[i]);
            drawDock(p, QRectF(col.left(), col.bottom() + 4, col.width(), kDockH - 8), lit);
            p.setPen(QColor(120, 125, 140));
            p.drawText(QRectF(col.left(), col.bottom() + kDockH - 14, col.width(), 14), Qt::AlignCenter,
                       QStringLiteral("#%1").arg(index));
        }
    }
    const QString out = QDir(outDir).filePath(QFileInfo(path).completeBaseName() + "_strip.png");
    strip.save(out);
    std::printf("png       %s\n", qPrintable(QDir::toNativeSeparators(out)));
    return 0;
}
