#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QString>

namespace fg::fdg {

/** IpcCommandEnum values in use, from Space Station 4.2.2.3's embedded schema; see docs/ipc-protocol.md. */
enum Cmd : int {
    Cmd_Unknown = 0,
    Cmd_Init = 1,
    Cmd_GetConnectedDevices = 4097,
    Cmd_DeviceConnectionChanged = 4098,
    Cmd_GetDeviceDetailInfo = 4099,
    Cmd_GetConfig = 4112,
    Cmd_UpdateConfig = 4113,
    Cmd_UploadProgressChanged = 4148,
    Cmd_DeviceNotSupported = 65536,
};

enum Category : int {
    Category_Unknown = 0,
    Category_Controller = 1,
    Category_Cooler = 2,
    Category_Keyboard = 3,
    Category_Mouse = 4,
    Category_Headset = 5,
    Category_Charger = 6,
};

enum ChargerLedType : int {
    ChargerLedType_Close = 0,
    ChargerLedType_Solid = 1,
    ChargerLedType_Default = 2,
    ChargerLedType_Custom = 3,
    ChargerLedType_Diagonal_Flow = 4,
    ChargerLedType_Breath = 5,
    ChargerLedType_Gradient = 6,
    ChargerLedType_Wave_Gradient = 7,
    ChargerLedType_Rainbow = 8,
    ChargerLedType_Pulse = 9,
};

enum ChargerLedDirection : int {
    ChargerLedDirection_NONE = 0,
    ChargerLedDirection_ToRight = 1,
    ChargerLedDirection_ToLeft = 2,
    ChargerLedDirection_ToDown = 3,
    ChargerLedDirection_ToUp = 4,
    ChargerLedDirection_ToRightDown = 5,
    ChargerLedDirection_ToRightUp = 6,
    ChargerLedDirection_ToLeftDown = 7,
    ChargerLedDirection_ToLeftUp = 8,
};

inline constexpr int kDockLedCount = 162;

/** Field numbers of the IpcResult oneof data members that are decoded. */
enum ResultData : int {
    Data_GetConnectedDevices = 5,       /**< GetConnectedDevicesResult */
    Data_DeviceConnectionChanged = 6,   /**< DeviceConnectionChangedResult */
    Data_GetDeviceDetailInfo = 7,       /**< GetDeviceDetailInfoResult */
    Data_InitResult = 54,
    Data_ProgressChanged = 56,          /**< ProgressChangedResult */
};

/** IpcResult {cmdId 1, state 2, code 3, category 4, oneof data 5..65536}. */
struct Result {
    int cmdId = 0;
    bool state = false;
    int code = 0;
    int category = 0;
    int dataField = 0;     /**< field number of the oneof member that is set, 0 for none */
    QByteArray data;       /**< that member's encoded bytes */
};

/** DeviceInfo from Device.proto. */
struct DeviceInfo {
    QString productName;
    int category = 0;
    int connectType = 0;
    QString deviceCode;       /**< "cd2" for the dock */
    int deviceType = 0;
    QString deviceMac;
    QString uid;
    bool isConnected = false;
    int battery = 0;
    QString firmwareVersion;
    int vendorId = 0;
    int productId = 0;
    bool isNewArchitecture = false;
};

bool parseResult(QByteArrayView payload, Result* out);
bool parseDeviceInfo(QByteArrayView bytes, DeviceInfo* out);
/** GetConnectedDevicesResult {repeated DeviceInfo connectedDevices = 1}. */
bool parseConnectedDevices(QByteArrayView bytes, QList<DeviceInfo>* out);
/** DeviceConnectionChangedResult {DeviceInfo deviceInfo = 1}. */
bool parseDeviceConnectionChanged(QByteArrayView bytes, DeviceInfo* out);
/** ProgressChangedResult {double progress = 1}, in percent. */
bool parseProgress(QByteArrayView bytes, double* out);

QByteArray buildGetConnectedDevices();

/**
 * The DIY page's UpdateConfig for a Custom animation, byte for byte; layout in docs/ipc-protocol.md.
 * @param rgb  frameCount * 162 * 3 bytes; not validated here.
 */
QByteArray buildUpdateConfigCustom(const QString& uid, const QByteArray& rgb, int frameCount,
                                   int period, int brightness);

QString cmdName(int cmdId);

} // namespace fg::fdg
