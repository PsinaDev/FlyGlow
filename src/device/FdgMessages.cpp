#include "FdgMessages.h"

#include "ProtoWire.h"

#include <cstring>

namespace fg::fdg {

namespace {

// IpcCommand fields
constexpr int kCmdId = 1;
constexpr int kCategory = 2;
constexpr int kUpdateConfigParam = 6;   // UpdateConfigParams

// UpdateConfigParams fields
constexpr int kUcUid = 2;
constexpr int kUcChargerLedConfig = 12;

// ChargerLedConfig fields
constexpr int kLedMode = 1;
constexpr int kLedPeriod = 2;
constexpr int kLedBrightness = 3;
constexpr int kLedFrames = 7;

// FramedLedColor / Color fields
constexpr int kFrameBrightness = 1;
constexpr int kFrameColors = 2;
constexpr int kRed = 1, kGreen = 2, kBlue = 3;

QByteArray command(int cmdId, int category)
{
    pw::Writer w;
    w.int32(kCmdId, cmdId);
    w.int32(kCategory, category, /*always=*/true);   // proto3 optional, so Space Station's UI always sends it
    return w.bytes();
}

} // namespace

QByteArray buildGetConnectedDevices()
{
    // Space Station sends category Controller here; the reply lists every device regardless.
    return command(Cmd_GetConnectedDevices, Category_Charger);
}

QByteArray buildUpdateConfigCustom(const QString& uid, const QByteArray& rgb, int frameCount,
                                   int period, int brightness)
{
    pw::Writer led;
    led.int32(kLedMode, ChargerLedType_Custom);
    led.int32(kLedPeriod, period);
    led.int32(kLedBrightness, brightness);
    // The DIY page also sets direction, useColorCount and palette, but their proto3 defaults never reach the wire.
    const auto* px = reinterpret_cast<const uint8_t*>(rgb.constData());
    for (int f = 0; f < frameCount; ++f) {
        pw::Writer frame;
        frame.int32(kFrameBrightness, 100);
        for (int i = 0; i < kDockLedCount; ++i) {
            const uint8_t* c = px + (qsizetype(f) * kDockLedCount + i) * 3;
            pw::Writer color;   // black is an empty but present Color
            color.int32(kRed, c[0]);
            color.int32(kGreen, c[1]);
            color.int32(kBlue, c[2]);
            frame.message(kFrameColors, color.bytes());
        }
        led.message(kLedFrames, frame.bytes());
    }

    pw::Writer param;
    param.string(kUcUid, uid);
    param.message(kUcChargerLedConfig, led.bytes());

    pw::Writer cmd;
    cmd.int32(kCmdId, Cmd_UpdateConfig);
    cmd.int32(kCategory, Category_Charger, true);
    cmd.message(kUpdateConfigParam, param.bytes());
    return cmd.bytes();
}

bool parseResult(QByteArrayView payload, Result* out)
{
    *out = Result{};
    pw::Reader r(payload);
    pw::Field f;
    while (r.next(f)) {
        switch (f.number) {
        case 1: out->cmdId = f.asInt32(); break;
        case 2: out->state = f.asBool(); break;
        case 3: out->code = f.asInt32(); break;
        case 4: out->category = f.asInt32(); break;
        default:
            if (f.number >= 5 && f.wireType == pw::LengthDelimited) {
                out->dataField = f.number;
                out->data = f.bytes.toByteArray();
            }
            break;
        }
    }
    return !r.error();
}

bool parseDeviceInfo(QByteArrayView bytes, DeviceInfo* out)
{
    *out = DeviceInfo{};
    pw::Reader r(bytes);
    pw::Field f;
    while (r.next(f)) {
        switch (f.number) {
        case 1: out->productName = f.asString(); break;
        case 2: out->category = f.asInt32(); break;
        case 3: out->connectType = f.asInt32(); break;
        case 4: out->deviceCode = f.asString(); break;
        case 5: out->deviceType = f.asInt32(); break;
        case 6: out->deviceMac = f.asString(); break;
        case 7: out->uid = f.asString(); break;
        case 8: out->isConnected = f.asBool(); break;
        case 9: out->battery = f.asInt32(); break;
        case 10: out->firmwareVersion = f.asString(); break;
        case 11: out->vendorId = f.asInt32(); break;
        case 12: out->productId = f.asInt32(); break;
        case 14: out->isNewArchitecture = f.asBool(); break;
        default: break;   // 13 = repeated FirmwareInfo
        }
    }
    return !r.error();
}

bool parseConnectedDevices(QByteArrayView bytes, QList<DeviceInfo>* out)
{
    out->clear();
    pw::Reader r(bytes);
    pw::Field f;
    while (r.next(f)) {
        if (f.number == 1 && f.wireType == pw::LengthDelimited) {
            DeviceInfo d;
            if (!parseDeviceInfo(f.bytes, &d))
                return false;
            out->append(d);
        }
    }
    return !r.error();
}

bool parseDeviceConnectionChanged(QByteArrayView bytes, DeviceInfo* out)
{
    pw::Reader r(bytes);
    pw::Field f;
    bool found = false;
    while (r.next(f)) {
        if (f.number == 1 && f.wireType == pw::LengthDelimited)
            found = parseDeviceInfo(f.bytes, out);
    }
    return found && !r.error();
}

bool parseProgress(QByteArrayView bytes, double* out)
{
    pw::Reader r(bytes);
    pw::Field f;
    *out = 0;
    while (r.next(f)) {
        if (f.number == 1 && f.wireType == pw::Fixed64) {
            const uint64_t bits = f.varint;
            std::memcpy(out, &bits, sizeof(double));
        }
    }
    return !r.error();
}

QString cmdName(int cmdId)
{
    switch (cmdId) {
    case Cmd_Unknown: return QStringLiteral("Unknown");
    case Cmd_Init: return QStringLiteral("Init");
    case Cmd_GetConnectedDevices: return QStringLiteral("GetConnectedDevices");
    case Cmd_DeviceConnectionChanged: return QStringLiteral("DeviceConnectionChanged");
    case Cmd_GetDeviceDetailInfo: return QStringLiteral("GetDeviceDetailInfo");
    case Cmd_GetConfig: return QStringLiteral("GetConfig");
    case Cmd_UpdateConfig: return QStringLiteral("UpdateConfig");
    case Cmd_UploadProgressChanged: return QStringLiteral("UploadProgressChanged");
    case Cmd_DeviceNotSupported: return QStringLiteral("DeviceNotSupported");
    default: return QStringLiteral("cmd %1").arg(cmdId);
    }
}

} // namespace fg::fdg
