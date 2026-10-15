#include "SpaceStationClient.h"

#include "FdgFraming.h"
#include "FdgMessages.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLocalSocket>
#include <QTimer>

#include <algorithm>
#include <optional>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace fg {

namespace {

// Uploads linger longer so quick successive ones reuse the pipe; the service needs ~200 ms to accept a new client.
constexpr int kIdleAfterQueryMs = 200;
constexpr int kIdleAfterUploadMs = 1500;
constexpr int kPollMs = 30000;
constexpr int kBackoffMinMs = 1000;
constexpr int kBackoffMaxMs = 30000;
constexpr int kRefreshReplyTimeoutMs = 5000;
constexpr int kUploadConnectGiveUpMs = 20000;
constexpr int kUploadReplyBaseMs = 15000;
constexpr int kUploadReplyPerFrameMs = 150;     // observed ~85 ms per frame
constexpr int kStaleResultWindowMs = 120000;
constexpr int kTickMs = 250;
constexpr int kMaxFrames = 255;
constexpr int kSafeFrames = 200;

const QString kDefaultPipeName = QStringLiteral("fcs.sock");

QString pipeName()
{
    const QString env = qEnvironmentVariable("FLYGLOW_FDG_PIPE").trimmed();
    return env.isEmpty() ? kDefaultPipeName : env;
}

int uploadReplyTimeout(int frames)
{
    return kUploadReplyBaseMs + kUploadReplyPerFrameMs * frames;
}

} // namespace

struct SpaceStationClient::Private {
    explicit Private(SpaceStationClient* qq) : q(qq) {}

    SpaceStationClient* q;
    const QString pipe = pipeName();
    QLocalSocket* sock = nullptr;
    fdg::FrameDecoder decoder;
    QTimer retryTimer;
    QTimer idleTimer;
    QTimer pollTimer;
    QTimer tickTimer;
    QElapsedTimer clock;

    bool started = false;
    bool reachable = false;
    bool pipeOpen = false;
    bool closingByUs = false;
    bool sessionUploaded = false;   // selects the longer idle linger
    int backoffMs = kBackoffMinMs;
    QString lastFailure;
    int failureRepeats = 0;

    struct Upload {
        int id = 0;
        QString uid;
        QByteArray framed;
        int frames = 0;
        qint64 queuedAt = 0;
        qint64 replyDeadline = 0;
    };
    QList<Upload> uploadQueue;
    std::optional<Upload> inFlight;

    bool refreshWanted = false;
    bool refreshInFlight = false;
    qint64 refreshDeadline = 0;

    int staleUpdateResults = 0;  // UpdateConfig replies already given up on
    qint64 staleUntil = 0;

    QList<DockInfo> docks;
    bool docksKnown = false;
    int nextId = 1;

    qint64 now() const { return clock.elapsed(); }
    void log(const QString& line) { emit q->logMessage(line); }

    bool hasWork() const
    {
        return refreshWanted || refreshInFlight || inFlight.has_value() || !uploadQueue.isEmpty();
    }

    void setReachable(bool r)
    {
        if (r == reachable)
            return;
        reachable = r;
        emit q->connectedChanged(r);
        if (!r && (!docks.isEmpty() || docksKnown)) {
            docksKnown = false;
            if (!docks.isEmpty()) {
                docks.clear();
                emit q->devicesChanged();
            }
        }
    }

    void finishUpload(const Upload& u, bool ok, const QString& message)
    {
        log(QStringLiteral("upload #%1 %2: %3")
                .arg(u.id)
                .arg(ok ? QStringLiteral("done") : QStringLiteral("FAILED"), message));
        emit q->uploadFinished(u.id, ok, message);
    }

    void pump()
    {
        if (!hasWork()) {
            tickTimer.stop();
            if (pipeOpen && !idleTimer.isActive())
                idleTimer.start(sessionUploaded ? kIdleAfterUploadMs : kIdleAfterQueryMs);
            return;
        }
        idleTimer.stop();
        if (!tickTimer.isActive())
            tickTimer.start();
        if (!pipeOpen) {
            if (!retryTimer.isActive() && !(sock && sock->state() == QLocalSocket::ConnectingState))
                tryConnect();
            return;
        }
        if (refreshWanted && !refreshInFlight) {
            refreshWanted = false;
            refreshInFlight = true;
            refreshDeadline = now() + kRefreshReplyTimeoutMs;
            send(fdg::frame(fdg::buildGetConnectedDevices()), QStringLiteral("GetConnectedDevices"));
        }
        if (!inFlight && !uploadQueue.isEmpty()) {
            Upload u = uploadQueue.takeFirst();
            u.replyDeadline = now() + uploadReplyTimeout(u.frames);
            inFlight = u;
            sessionUploaded = true;
            send(u.framed, QStringLiteral("UpdateConfig (upload #%1: %2 frames, %3 bytes) to %4")
                               .arg(u.id).arg(u.frames).arg(u.framed.size()).arg(u.uid));
        }
    }

    void send(const QByteArray& framed, const QString& what)
    {
        const qint64 n = sock->write(framed);
        if (n != framed.size())
            log(QStringLiteral("write failed (%1): %2").arg(what, sock->errorString()));
        else
            log(QStringLiteral("-> %1").arg(what));
    }

    void onTick()
    {
        const qint64 t = now();
        if (refreshInFlight && t > refreshDeadline) {
            refreshInFlight = false;
            log(QStringLiteral("no reply to GetConnectedDevices within %1 ms").arg(kRefreshReplyTimeoutMs));
        }
        if (inFlight && t > inFlight->replyDeadline) {
            const Upload u = *inFlight;
            inFlight.reset();
            ++staleUpdateResults;
            staleUntil = t + kStaleResultWindowMs;
            finishUpload(u, false,
                         QCoreApplication::translate("SpaceStationClient",
                                                     "Space Station did not confirm the upload within %1 s.")
                             .arg(uploadReplyTimeout(u.frames) / 1000));
        }
        if (!pipeOpen) {
            for (qsizetype i = 0; i < uploadQueue.size();) {
                if (t - uploadQueue[i].queuedAt > kUploadConnectGiveUpMs) {
                    const Upload u = uploadQueue.takeAt(i);
                    const QString why = lastFailure.isEmpty()
                                            ? QCoreApplication::translate("SpaceStationClient", "no connection")
                                            : lastFailure;
                    finishUpload(u, false,
                                 QCoreApplication::translate("SpaceStationClient",
                                                             "Could not reach the Space Station service: %1")
                                     .arg(why));
                } else {
                    ++i;
                }
            }
        }
        pump();
    }

    void tryConnect()
    {
        if (!sock) {
            sock = new QLocalSocket(q);
            QObject::connect(sock, &QLocalSocket::readyRead, q, [this] { onReadyRead(); });
            QObject::connect(sock, &QLocalSocket::disconnected, q, [this] { onDisconnected(); });
            QObject::connect(sock, &QLocalSocket::errorOccurred, q, [this](QLocalSocket::LocalSocketError e) {
                if (!pipeOpen && e != QLocalSocket::PeerClosedError) {
                    onConnectFailed(sock->errorString());
                    return;
                }
                if (e != QLocalSocket::PeerClosedError && !closingByUs)
                    log(QStringLiteral("pipe error: %1").arg(sock->errorString()));
            });
#ifndef Q_OS_WIN
            QObject::connect(sock, &QLocalSocket::connected, q, [this] { onPipeOpened(); });
#endif
        }
        closingByUs = false;
#ifdef Q_OS_WIN
        // connectToServer() blocks in WaitNamedPipe for up to 5 s while the pipe is busy, so open the handle as Qt does.
        const std::wstring path = L"\\\\.\\pipe\\" + pipe.toStdWString();
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OVERLAPPED, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            const DWORD err = GetLastError();
            QString why;
            switch (err) {
            case ERROR_FILE_NOT_FOUND:
                // Also true for ~200 ms after a client leaves, while the service re-creates the pipe.
                why = QCoreApplication::translate("SpaceStationClient",
                                                  "The pipe %1 was not found. Is SpaceStationService running?")
                          .arg(QStringLiteral("\\\\.\\pipe\\") + pipe);
                break;
            case ERROR_PIPE_BUSY:
                why = QCoreApplication::translate("SpaceStationClient",
                                                  "The pipe is busy. The service serves one client at a time, "
                                                  "and Space Station's own window is most likely open.");
                break;
            case ERROR_ACCESS_DENIED:
                why = QCoreApplication::translate("SpaceStationClient", "Access to the pipe was denied.");
                break;
            default:
                why = QCoreApplication::translate("SpaceStationClient", "Could not open the pipe (Windows error %1).")
                          .arg(err);
                break;
            }
            onConnectFailed(why);
            return;
        }
        if (!sock->setSocketDescriptor(reinterpret_cast<qintptr>(h), QLocalSocket::ConnectedState,
                                       QIODevice::ReadWrite)) {
            CloseHandle(h);
            onConnectFailed(QCoreApplication::translate("SpaceStationClient", "Could not use the pipe handle."));
            return;
        }
        onPipeOpened();
#else
        sock->connectToServer(pipe);
#endif
    }

    void onConnectFailed(const QString& why)
    {
        if (why == lastFailure) {
            ++failureRepeats;
            if (failureRepeats % 10 == 0)
                log(QStringLiteral("still cannot connect (%1 attempts): %2").arg(failureRepeats + 1).arg(why));
        } else {
            lastFailure = why;
            failureRepeats = 0;
            log(QStringLiteral("cannot connect: %1").arg(why));
        }
        setReachable(false);
        if (hasWork()) {
            retryTimer.start(backoffMs);
            backoffMs = qMin(backoffMs * 2, kBackoffMaxMs);
        }
    }

    void onPipeOpened()
    {
        pipeOpen = true;   // backoff is reset only once the service actually answers
        lastFailure.clear();
        failureRepeats = 0;
        retryTimer.stop();
        decoder.clear();
        log(QStringLiteral("connected to \\\\.\\pipe\\%1").arg(pipe));
        setReachable(true);
        pump();
    }

    void onDisconnected()
    {
        if (!pipeOpen)
            return;
        pipeOpen = false;
        sessionUploaded = false;
        decoder.clear();
        const bool byUs = closingByUs;
        closingByUs = false;
        log(byUs ? QStringLiteral("released the pipe (idle)")
                 : QStringLiteral("the service closed the pipe"));
        if (refreshInFlight) {
            refreshInFlight = false;
            refreshWanted = true;
        }
        if (inFlight) {
            const Upload u = *inFlight;
            inFlight.reset();
            ++staleUpdateResults;
            staleUntil = now() + kStaleResultWindowMs;
            finishUpload(u, false,
                         QCoreApplication::translate("SpaceStationClient",
                                                     "The connection to the Space Station service was lost before it "
                                                     "confirmed the upload. The dock may still have been updated."));
        }
        if (!byUs)
            refreshWanted = true;   // find out whether the service is still there
        if (started || hasWork())
            QTimer::singleShot(0, q, [this] { pump(); });
    }

    void releasePipe()
    {
        if (!pipeOpen || hasWork())
            return;
        closingByUs = true;
        sock->disconnectFromServer();
    }

    void onReadyRead()
    {
        decoder.append(sock->readAll());
        for (;;) {
            QByteArray payload;
            QString kind, error;
            const auto st = decoder.next(&payload, &kind, &error);
            if (st == fdg::FrameDecoder::NeedMore)
                break;
            if (st == fdg::FrameDecoder::Error) {
                log(QStringLiteral("framing error: %1").arg(error));
                continue;
            }
            fdg::Result r;
            if (!fdg::parseResult(payload, &r)) {
                log(QStringLiteral("undecodable %1 message (%2 bytes)").arg(kind).arg(payload.size()));
                continue;
            }
            backoffMs = kBackoffMinMs;
            handleResult(r, kind, payload.size());
        }
        pump();
    }

    void handleResult(const fdg::Result& r, const QString& kind, qsizetype size)
    {
        switch (r.cmdId) {
        case fdg::Cmd_GetConnectedDevices: {
            refreshInFlight = false;
            QList<fdg::DeviceInfo> devices;
            if (r.dataField != fdg::Data_GetConnectedDevices || !fdg::parseConnectedDevices(r.data, &devices)) {
                log(QStringLiteral("<- GetConnectedDevices: state=%1, no device list").arg(r.state));
                if (r.dataField == 0)
                    setDocks({});
                return;
            }
            QList<DockInfo> list;
            for (const fdg::DeviceInfo& d : devices) {
                if (d.category != fdg::Category_Charger)
                    continue;
                if (d.deviceCode != QLatin1String("cd2")) {
                    log(QStringLiteral("ignoring charger \"%1\" (deviceCode %2): not a CD2")
                            .arg(d.productName, d.deviceCode));
                    continue;
                }
                if (!d.isConnected)
                    continue;
                list.append({d.uid, d.productName.isEmpty() ? QStringLiteral("Flydigi CD2") : d.productName,
                             d.firmwareVersion});
            }
            log(QStringLiteral("<- GetConnectedDevices: %1 device(s), %2 CD2 dock(s)")
                    .arg(devices.size()).arg(list.size()));
            setDocks(list);
            return;
        }
        case fdg::Cmd_DeviceConnectionChanged: {
            fdg::DeviceInfo d;
            if (r.dataField != fdg::Data_DeviceConnectionChanged || !fdg::parseDeviceConnectionChanged(r.data, &d))
                return;
            log(QStringLiteral("<- DeviceConnectionChanged: %1 %2 %3")
                    .arg(d.productName, d.uid,
                         d.isConnected ? QStringLiteral("connected") : QStringLiteral("disconnected")));
            if (d.category == fdg::Category_Charger && d.deviceCode == QLatin1String("cd2")) {
                QList<DockInfo> list = docks;
                list.erase(std::remove_if(list.begin(), list.end(),
                                          [&](const DockInfo& k) { return k.uid == d.uid; }),
                           list.end());
                if (d.isConnected)
                    list.append({d.uid, d.productName, d.firmwareVersion});
                setDocks(list);
            }
            return;
        }
        case fdg::Cmd_UpdateConfig: {
            if (staleUpdateResults > 0 && now() < staleUntil) {
                --staleUpdateResults;
                log(QStringLiteral("<- late UpdateConfig result (state=%1) for an upload already reported as "
                                   "failed; ignored")
                        .arg(r.state));
                return;
            }
            staleUpdateResults = 0;
            if (!inFlight) {
                log(QStringLiteral("<- UpdateConfig result (state=%1) with no upload pending; ignored").arg(r.state));
                return;
            }
            const Upload u = *inFlight;
            inFlight.reset();
            finishUpload(u, r.state,
                         r.state ? QCoreApplication::translate("SpaceStationClient", "Space Station reported success.")
                                 : QCoreApplication::translate("SpaceStationClient",
                                                               "Space Station reported a failure (code %1).")
                                       .arg(r.code));
            return;
        }
        case fdg::Cmd_UploadProgressChanged: {
            double p = 0;
            if (inFlight && r.dataField == fdg::Data_ProgressChanged && fdg::parseProgress(r.data, &p))
                emit q->uploadProgress(inFlight->id, qBound(0.0, p / 100.0, 1.0));
            return;
        }
        default:
            // e.g. GetDeviceDetailInfo, which the service pushes unprompted when the dock's state changes.
            log(QStringLiteral("<- %1 (%2, %3 bytes, state=%4): not used, ignored")
                    .arg(fdg::cmdName(r.cmdId), kind).arg(size).arg(r.state));
            return;
        }
    }

    void setDocks(const QList<DockInfo>& list)
    {
        auto same = [](const QList<DockInfo>& a, const QList<DockInfo>& b) {
            if (a.size() != b.size())
                return false;
            for (qsizetype i = 0; i < a.size(); ++i)
                if (a[i].uid != b[i].uid || a[i].name != b[i].name || a[i].firmware != b[i].firmware)
                    return false;
            return true;
        };
        const bool changed = !docksKnown || !same(list, docks);
        docks = list;
        docksKnown = true;
        if (changed)
            emit q->devicesChanged();
    }
};

SpaceStationClient::SpaceStationClient(QObject* parent) : QObject(parent), d(new Private(this))
{
    d->clock.start();
    d->retryTimer.setSingleShot(true);
    d->idleTimer.setSingleShot(true);
    d->pollTimer.setInterval(kPollMs);
    d->tickTimer.setInterval(kTickMs);
    connect(&d->retryTimer, &QTimer::timeout, this, [this] { d->pump(); });
    connect(&d->idleTimer, &QTimer::timeout, this, [this] { d->releasePipe(); });
    connect(&d->pollTimer, &QTimer::timeout, this, [this] { refreshDevices(); });
    connect(&d->tickTimer, &QTimer::timeout, this, [this] { d->onTick(); });
}

SpaceStationClient::~SpaceStationClient()
{
    if (d->sock) {
        d->sock->disconnect(this);
        d->sock->abort();
    }
    delete d;
}

void SpaceStationClient::start()
{
    if (d->started)
        return;
    d->started = true;
    if (d->pipe != kDefaultPipeName)
        d->log(QStringLiteral("FLYGLOW_FDG_PIPE: using \\\\.\\pipe\\%1 instead of %2").arg(d->pipe, kDefaultPipeName));
    d->pollTimer.start();
    refreshDevices();
}

bool SpaceStationClient::isConnected() const
{
    return d->reachable;
}

QList<SpaceStationClient::DockInfo> SpaceStationClient::docks() const
{
    return d->docks;
}

void SpaceStationClient::refreshDevices()
{
    d->refreshWanted = true;
    if (!d->pipeOpen && d->retryTimer.isActive()) {
        // An explicit refresh retries now instead of waiting out the backoff.
        d->retryTimer.stop();
    }
    d->pump();
}

int SpaceStationClient::uploadCustom(const QString& uid, const QByteArray& rgb, int frameCount, int period,
                                     int brightness)
{
    const int id = d->nextId++;
    auto reject = [this, id](const QString& why) {
        d->log(QStringLiteral("upload #%1 rejected: %2").arg(id).arg(why));
        QTimer::singleShot(0, this, [this, id, why] { emit uploadFinished(id, false, why); });
        return id;
    };
    if (uid.isEmpty())
        return reject(QCoreApplication::translate("SpaceStationClient", "No dock was given for the upload."));
    if (frameCount < 1 || frameCount > kMaxFrames)
        return reject(QCoreApplication::translate("SpaceStationClient", "The frame count %1 is outside 1..%2.")
                          .arg(frameCount)
                          .arg(kMaxFrames));
    const qsizetype expected = qsizetype(frameCount) * fdg::kDockLedCount * 3;
    if (rgb.size() != expected)
        return reject(QCoreApplication::translate("SpaceStationClient",
                                                  "The animation data is %1 bytes instead of %2 "
                                                  "(%3 frames × %4 LEDs × 3).")
                          .arg(rgb.size())
                          .arg(expected)
                          .arg(frameCount)
                          .arg(fdg::kDockLedCount));
    if (period < 1 || period > 255) {
        d->log(QStringLiteral("upload #%1: period %2 clamped to 1..255").arg(id).arg(period));
        period = qBound(1, period, 255);
    }
    if (brightness < 1 || brightness > 100) {
        d->log(QStringLiteral("upload #%1: brightness %2 clamped to 1..100").arg(id).arg(brightness));
        brightness = qBound(1, brightness, 100);
    }
    if (frameCount > kSafeFrames)
        d->log(QStringLiteral("upload #%1: %2 frames is above the %3 known to be safe")
                   .arg(id).arg(frameCount).arg(kSafeFrames));
    if (frameCount % 25 == 4)
        d->log(QStringLiteral("upload #%1: warning: with %2 frames the config blob is a multiple of 50 bytes; "
                              "Space Station then announces one more HID pack than it sends (see "
                              "docs/ipc-protocol.md)")
                   .arg(id).arg(frameCount));
    bool known = false;
    for (const DockInfo& k : d->docks)
        known = known || k.uid == uid;
    if (!known)
        d->log(QStringLiteral("upload #%1: uid %2 is not in the current dock list; sending anyway").arg(id).arg(uid));

    Private::Upload u;
    u.id = id;
    u.uid = uid;
    u.frames = frameCount;
    u.framed = fdg::frame(fdg::buildUpdateConfigCustom(uid, rgb, frameCount, period, brightness));
    u.queuedAt = d->now();

    for (qsizetype i = 0; i < d->uploadQueue.size();) {
        if (d->uploadQueue[i].uid == uid) {
            const Private::Upload old = d->uploadQueue.takeAt(i);
            QTimer::singleShot(0, this, [this, old, id] {
                d->finishUpload(old, false,
                                QCoreApplication::translate("SpaceStationClient",
                                                            "Replaced by a newer upload (#%1).")
                                    .arg(id));
            });
        } else {
            ++i;
        }
    }
    d->uploadQueue.append(u);
    if (!d->pipeOpen && d->retryTimer.isActive())
        d->retryTimer.stop();   // try right away rather than after the backoff
    d->pump();
    return id;
}

} // namespace fg
