#include "device/FdgFraming.h"
#include "device/FdgMessages.h"
#include "device/ProtoWire.h"
#include "device/SpaceStationClient.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QRandomGenerator>
#include <QSet>
#include <QTextStream>
#include <QTimer>

#include <cmath>
#include <cstdio>

using namespace fg;

namespace {

QTextStream& out()
{
    static QTextStream s(stdout);
    return s;
}

QByteArray softGradient(int frames)
{
    QByteArray rgb(qsizetype(frames) * fdg::kDockLedCount * 3, Qt::Uninitialized);
    const double a[3] = {255, 140, 90}, b[3] = {90, 140, 255};
    for (int f = 0; f < frames; ++f) {
        for (int i = 0; i < fdg::kDockLedCount; ++i) {
            double t = double(i) / (fdg::kDockLedCount - 1) + double(f) / frames;
            t -= std::floor(t);
            const double s = 0.5 - 0.5 * std::cos(2 * 3.14159265358979323846 * t);
            for (int c = 0; c < 3; ++c) {
                const double v = 0.6 * (a[c] * (1 - s) + b[c] * s);
                rgb[(qsizetype(f) * fdg::kDockLedCount + i) * 3 + c] = char(qBound(0, int(std::lround(v)), 255));
            }
        }
    }
    return rgb;
}

int decodeFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        out() << "cannot open " << path << "\n";
        return 1;
    }
    fdg::FrameDecoder dec;
    dec.append(f.readAll());
    int n = 0;
    for (;;) {
        QByteArray payload;
        QString kind, error;
        const auto st = dec.next(&payload, &kind, &error);
        if (st == fdg::FrameDecoder::NeedMore)
            break;
        if (st == fdg::FrameDecoder::Error) {
            out() << "framing error: " << error << "\n";
            continue;
        }
        ++n;
        fdg::Result r;
        const bool ok = fdg::parseResult(payload, &r);
        out() << "message " << n << ": " << kind << ", " << payload.size() << " bytes payload"
              << (ok ? "" : " (protobuf error)") << "\n"
              << "  IpcResult cmdId=" << fdg::cmdName(r.cmdId) << " state=" << (r.state ? "true" : "false")
              << " code=" << r.code << " category=" << r.category << " data field=" << r.dataField << " ("
              << r.data.size() << " bytes)\n";
        if (r.dataField == fdg::Data_GetConnectedDevices) {
            QList<fdg::DeviceInfo> devs;
            fdg::parseConnectedDevices(r.data, &devs);
            for (const auto& d : devs)
                out() << "    device " << d.productName << " category=" << d.category << " code=" << d.deviceCode
                      << " uid=" << d.uid << " fw=" << d.firmwareVersion << " connected=" << d.isConnected << "\n";
        }
    }
    if (dec.buffered())
        out() << dec.buffered() << " trailing bytes (incomplete message)\n";
    out().flush();
    return n > 0 ? 0 : 1;
}

int selftest()
{
    int failures = 0;
    auto check = [&](bool ok, const QString& what) {
        out() << (ok ? "  ok    " : "  FAIL  ") << what << "\n";
        failures += ok ? 0 : 1;
    };
    auto rnd = [](qsizetype n) {
        QByteArray b(n, Qt::Uninitialized);
        auto* g = QRandomGenerator::global();
        for (qsizetype i = 0; i < n; ++i)
            b[i] = char(g->bounded(256));
        return b;
    };
    auto roundTrip = [&](const QByteArray& framed, const QByteArray& payload, const QString& expectKind,
                         int splitEvery) {
        fdg::FrameDecoder dec;
        QByteArray got;
        QString kind, err;
        int messages = 0;
        for (qsizetype pos = 0; pos < framed.size(); pos += splitEvery) {
            dec.append(framed.mid(pos, splitEvery));
            QByteArray p;
            QString k;
            while (true) {
                const auto st = dec.next(&p, &k, &err);
                if (st == fdg::FrameDecoder::NeedMore)
                    break;
                if (st == fdg::FrameDecoder::Message) {
                    got = p;
                    kind = k;
                    ++messages;
                }
            }
        }
        return messages == 1 && kind == expectKind && got == payload;
    };

    out() << "framing\n";
    const QByteArray small = rnd(1000), mid = rnd(fdg::kChunkThreshold + 12345),
                     big = rnd(fdg::kCompressThreshold + 777);
    check(fdg::frame(small).startsWith("FDG_PROTOCOL\n"), "small payload -> FDG_PROTOCOL");
    check(fdg::frame(mid).startsWith("FDG_CHUNK\n"), "1 MiB+ payload -> FDG_CHUNK");
    check(fdg::frame(big).startsWith("FDG_COMPRESSED\n"), "5 MiB+ payload -> FDG_COMPRESSED");
    check(roundTrip(fdg::frame(small), small, "small", 7), "small round trip, 7-byte reads");
    check(roundTrip(fdg::frame(mid), mid, "chunked", 4096), "chunked round trip, 4 KiB reads");
    check(roundTrip(fdg::frame(big), big, "compressed", 1 << 20), "compressed (stored gzip) round trip");
    {
        fdg::FrameDecoder dec;
        dec.append(QByteArray("garbage\x01\x02") + fdg::frame(small));
        QByteArray p;
        QString k, e;
        int errors = 0, msgs = 0;
        for (;;) {
            const auto st = dec.next(&p, &k, &e);
            if (st == fdg::FrameDecoder::NeedMore)
                break;
            (st == fdg::FrameDecoder::Error ? errors : msgs)++;
        }
        check(errors >= 1 && msgs == 1 && p == small, "resync after garbage");
    }
    {
        QByteArray z;
        QString e;
        check(fdg::gunzip(fdg::gzipStored(QByteArray()), &z, &e) && z.isEmpty(), "gzip of empty data");
        QByteArray bad = fdg::gzipStored(small);
        bad[bad.size() - 6] = char(bad[bad.size() - 6] ^ 0x55);
        const bool rejected = !fdg::gunzip(bad, &z, &e);
        check(rejected, "gzip CRC mismatch detected (" + e + ")");
    }

    out() << "encoder\n";
    const QByteArray rgb = softGradient(2);
    const QByteArray msg = fdg::buildUpdateConfigCustom("uid123", rgb, 2, 5, 80);
    int cmdId = 0, category = -1, frames = 0, colors = 0, mode = -1, period = 0, brightness = 0;
    bool firstColorOk = false;
    pw::Reader r(msg);
    pw::Field f;
    while (r.next(f)) {
        if (f.number == 1)
            cmdId = f.asInt32();
        if (f.number == 2)
            category = f.asInt32();
        if (f.number != 6)
            continue;
        pw::Reader p(f.bytes);
        pw::Field g;
        while (p.next(g)) {
            if (g.number != 12)
                continue;
            pw::Reader l(g.bytes);
            pw::Field h;
            while (l.next(h)) {
                if (h.number == 1)
                    mode = h.asInt32();
                if (h.number == 2)
                    period = h.asInt32();
                if (h.number == 3)
                    brightness = h.asInt32();
                if (h.number != 7)
                    continue;
                ++frames;
                pw::Reader fr(h.bytes);
                pw::Field c;
                while (fr.next(c)) {
                    if (c.number != 2)
                        continue;
                    if (colors == 0) {
                        int rgbv[4] = {0, 0, 0, 0};
                        pw::Reader cr(c.bytes);
                        pw::Field v;
                        while (cr.next(v))
                            if (v.number >= 1 && v.number <= 3)
                                rgbv[v.number] = v.asInt32();
                        firstColorOk = rgbv[1] == uint8_t(rgb[0]) && rgbv[2] == uint8_t(rgb[1]) &&
                                       rgbv[3] == uint8_t(rgb[2]);
                    }
                    ++colors;
                }
            }
        }
    }
    check(!r.error() && cmdId == fdg::Cmd_UpdateConfig && category == fdg::Category_Charger,
          "IpcCommand cmdId=UpdateConfig category=Charger");
    check(mode == fdg::ChargerLedType_Custom && period == 5 && brightness == 80, "chargerLedConfig header");
    check(frames == 2 && colors == 2 * fdg::kDockLedCount && firstColorOk, "2 frames x 162 colours");
    {
        const QByteArray black(fdg::kDockLedCount * 3, '\0');
        const QByteArray m = fdg::buildUpdateConfigCustom("u", black, 1, 1, 100);
        check(m.count(QByteArray("\x12\x00", 2)) >= fdg::kDockLedCount, "black LEDs encoded as empty Color");
    }
    out() << (failures ? "SELFTEST FAILED\n" : "selftest passed\n");
    out().flush();
    return failures ? 1 : 0;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("ipc_probe");

    QCommandLineParser cli;
    cli.setApplicationDescription("Lists the CD2 docks Space Station's service reports. It never uploads, except with\n"
                                  "--test-upload against a fake service. Check a --dry-run-upload file with:\n"
                                  "  python -I tools/fdg_ipc.py decode out.bin");
    cli.addHelpOption();
    QCommandLineOption dryRun("dry-run-upload",
                              "Write the framed UpdateConfig for a test animation to <file> instead of sending it.",
                              "file");
    QCommandLineOption uidOpt("uid", "Dock uid for --dry-run-upload (default: first dock found).", "uid");
    QCommandLineOption framesOpt("frames", "Frames in the test animation (default 3).", "n", "3");
    QCommandLineOption rgbOpt("rgb", "Use raw RGB frames from <file> (frames x 162 x 3 bytes) instead of the "
                                     "test gradient.", "file");
    QCommandLineOption periodOpt("period", "Frame period in 20 ms units (default 5).", "p", "5");
    QCommandLineOption brightOpt("brightness", "Brightness 1..100 (default 100).", "b", "100");
    QCommandLineOption timeoutOpt("timeout", "How long to wait for the service, ms (default 8000).", "ms", "8000");
    QCommandLineOption decodeOpt("decode", "Decode a file of framed IpcResult messages and exit.", "file");
    QCommandLineOption selftestOpt("selftest", "Run framing/encoder self tests and exit.");
    QCommandLineOption watchOpt("watch", "Keep the client running for <s> seconds (shows idle release, "
                                         "polling and reconnects), then report.", "s");
    QCommandLineOption testUploadOpt("test-upload",
                                     "Really upload <n> test animations back to back -- ONLY allowed against a "
                                     "fake service (FLYGLOW_FDG_PIPE set, see tools/fdg_fake_service.py).",
                                     "n");
    cli.addOptions({dryRun, uidOpt, framesOpt, rgbOpt, periodOpt, brightOpt, timeoutOpt, decodeOpt, selftestOpt,
                    watchOpt, testUploadOpt});
    cli.process(app);

    if (cli.isSet(selftestOpt))
        return selftest();
    if (cli.isSet(decodeOpt))
        return decodeFile(cli.value(decodeOpt));
    const QString pipeOverride = qEnvironmentVariable("FLYGLOW_FDG_PIPE").trimmed();
    if (cli.isSet(testUploadOpt) && (pipeOverride.isEmpty() || pipeOverride == QLatin1String("fcs.sock"))) {
        out() << "--test-upload refuses to talk to the real service; set FLYGLOW_FDG_PIPE to a fake pipe\n";
        out().flush();
        return 2;
    }

    SpaceStationClient client;
    QObject::connect(&client, &SpaceStationClient::logMessage, [](const QString& line) {
        out() << "[client] " << line << "\n";
        out().flush();
    });

    int exitCode = 1;
    auto finish = [&] {
        const auto docks = client.docks();
        out() << "\nservice reachable: " << (client.isConnected() ? "yes" : "no") << "\n";
        out() << "docks: " << docks.size() << "\n";
        for (const auto& k : docks)
            out() << "  uid=" << k.uid << "  name=\"" << k.name << "\"  firmware=" << k.firmware << "\n";
        exitCode = client.isConnected() ? 0 : 1;

        if (cli.isSet(dryRun)) {
            QString uid = cli.value(uidOpt);
            if (uid.isEmpty() && !docks.isEmpty())
                uid = docks.first().uid;
            if (uid.isEmpty()) {
                out() << "dry run: no dock uid (pass --uid)\n";
                exitCode = 1;
            } else {
                int frames = qBound(1, cli.value(framesOpt).toInt(), 255);
                QByteArray rgb;
                if (cli.isSet(rgbOpt)) {
                    QFile rf(cli.value(rgbOpt));
                    if (rf.open(QIODevice::ReadOnly))
                        rgb = rf.readAll();
                    const int frameBytes = fdg::kDockLedCount * 3;
                    if (rgb.isEmpty() || rgb.size() % frameBytes || rgb.size() / frameBytes > 255) {
                        out() << "dry run: " << rf.fileName() << " must hold 1..255 frames of " << frameBytes
                              << " bytes\n";
                        out().flush();
                        QCoreApplication::exit(1);
                        return;
                    }
                    frames = int(rgb.size() / frameBytes);
                } else {
                    rgb = softGradient(frames);
                }
                const int period = qBound(1, cli.value(periodOpt).toInt(), 255);
                const int brightness = qBound(1, cli.value(brightOpt).toInt(), 100);
                const QByteArray payload = fdg::buildUpdateConfigCustom(uid, rgb, frames, period, brightness);
                const QByteArray framed = fdg::frame(payload);
                QFile f(cli.value(dryRun));
                if (f.open(QIODevice::WriteOnly) && f.write(framed) == framed.size()) {
                    out() << "dry run: wrote " << framed.size() << " bytes (" << payload.size()
                          << " bytes protobuf, header " << framed.left(framed.indexOf('\n')) << ") for " << frames
                          << " frames, period " << period << ", brightness " << brightness << ", uid " << uid
                          << " to " << f.fileName() << " -- NOT sent\n";
                } else {
                    out() << "dry run: cannot write " << f.fileName() << "\n";
                    exitCode = 1;
                }
            }
        }
        if (cli.isSet(testUploadOpt) && !docks.isEmpty()) {
            const int n = qMax(1, cli.value(testUploadOpt).toInt());
            const int frames = qBound(1, cli.value(framesOpt).toInt(), 255);
            auto* pending = new QSet<int>;
            QObject::connect(&client, &SpaceStationClient::uploadFinished, &app,
                             [pending](int id, bool ok, const QString& msg) {
                                 out() << "uploadFinished(" << id << ", " << (ok ? "true" : "false") << ", \"" << msg
                                       << "\")\n";
                                 out().flush();
                                 pending->remove(id);
                                 if (pending->isEmpty()) {
                                     delete pending;
                                     QCoreApplication::exit(0);
                                 }
                             });
            QObject::connect(&client, &SpaceStationClient::uploadProgress, &app, [](int id, double f) {
                out() << "uploadProgress(" << id << ", " << f << ")\n";
            });
            for (int i = 0; i < n; ++i)
                pending->insert(client.uploadCustom(docks.first().uid, softGradient(frames), frames,
                                                    cli.value(periodOpt).toInt(), cli.value(brightOpt).toInt()));
            out().flush();
            return;   // the uploadFinished handler exits once every upload is done
        }
        out().flush();
        QCoreApplication::exit(exitCode);
    };

    bool done = false;
    auto finishOnce = [&] {
        if (!done) {
            done = true;
            // Let the client log the reply before the report.
            QTimer::singleShot(50, &app, finish);
        }
    };
    if (cli.isSet(watchOpt)) {
        QElapsedTimer since;
        since.start();
        auto stamp = [since] { return QString::number(since.elapsed() / 1000.0, 'f', 1) + "s"; };
        QObject::connect(&client, &SpaceStationClient::connectedChanged, [stamp](bool c) {
            out() << "[" << stamp() << "] connectedChanged(" << (c ? "true" : "false") << ")\n";
            out().flush();
        });
        QObject::connect(&client, &SpaceStationClient::devicesChanged, [&client, stamp] {
            out() << "[" << stamp() << "] devicesChanged: " << client.docks().size() << " dock(s)\n";
            out().flush();
        });
        QTimer::singleShot(cli.value(watchOpt).toInt() * 1000, &app, finishOnce);
    } else {
        QObject::connect(&client, &SpaceStationClient::devicesChanged, &app, finishOnce);
        QTimer::singleShot(cli.value(timeoutOpt).toInt(), &app, finishOnce);
    }

    client.start();
    const int rc = app.exec();
    return rc;
}
