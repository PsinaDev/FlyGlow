#include "HidDock.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <vector>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>

namespace fg {

namespace {

constexpr USHORT kVendorId = 0x37D7;
constexpr USHORT kProductId = 0x6001;
constexpr int kLedCount = 162;
constexpr int kPackBytes = 50;
constexpr quint8 kCmdHeartbeat = 1;
constexpr quint8 kCmdLedStart = 97;
constexpr quint8 kCmdLedPack = 98;
constexpr quint8 kModeCustom = 3;

struct DevicePath {
    std::wstring path;
    int outputLength = 0;
    int inputLength = 0;
};

bool findDock(DevicePath& out)
{
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
        return false;

    bool found = false;
    SP_DEVICE_INTERFACE_DATA iface{sizeof(SP_DEVICE_INTERFACE_DATA)};
    for (DWORD i = 0; !found && SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, i, &iface); ++i) {
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &iface, nullptr, 0, &size, nullptr);
        std::vector<char> buffer(size);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, size, nullptr, nullptr))
            continue;

        // Zero access rights: enough for attributes and caps, and never blocked by other openers.
        HANDLE h = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                               0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            continue;
        HIDD_ATTRIBUTES attrs{sizeof(HIDD_ATTRIBUTES)};
        if (HidD_GetAttributes(h, &attrs) && attrs.VendorID == kVendorId && attrs.ProductID == kProductId) {
            PHIDP_PREPARSED_DATA preparsed = nullptr;
            HIDP_CAPS caps{};
            if (HidD_GetPreparsedData(h, &preparsed)) {
                if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS && caps.OutputReportByteLength >= 33) {
                    out.path = detail->DevicePath;
                    out.outputLength = caps.OutputReportByteLength;
                    out.inputLength = caps.InputReportByteLength;
                    found = true;
                }
                HidD_FreePreparsedData(preparsed);
            }
        }
        CloseHandle(h);
    }
    SetupDiDestroyDeviceInfoList(set);
    return found;
}

} // namespace

class HidDockWorker : public QObject {
    Q_OBJECT
public:
    ~HidDockWorker() override { close(); }

public slots:
    void poll()
    {
        if (m_handle != INVALID_HANDLE_VALUE)
            return;
        DevicePath dev;
        const bool present = findDock(dev);
        if (present != m_present) {
            m_present = present;
            emit presenceChanged(present);
        }
    }

    void upload(int id, const QByteArray& rgb, int frameCount, int period, int brightness)
    {
        QString error;
        const bool ok = doUpload(id, rgb, frameCount, period, brightness, &error);
        close();
        emit uploadFinished(id, ok, error);
    }

signals:
    void presenceChanged(bool present);
    void uploadProgress(int id, double fraction);
    void uploadFinished(int id, bool ok, const QString& message);

private:
    bool open(QString* error)
    {
        DevicePath dev;
        if (!findDock(dev)) {
            *error = QCoreApplication::translate("HidDock", "The CD2 dock is not connected.");
            return false;
        }
        m_handle = CreateFileW(dev.path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (m_handle == INVALID_HANDLE_VALUE) {
            *error = QCoreApplication::translate("HidDock", "Cannot open the dock (error %1).").arg(GetLastError());
            return false;
        }
        m_outputLength = dev.outputLength;
        m_inputLength = std::max(dev.inputLength, 65);
        m_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        return true;
    }

    void close()
    {
        if (m_handle != INVALID_HANDLE_VALUE) {
            CancelIoEx(m_handle, nullptr);
            CloseHandle(m_handle);
            m_handle = INVALID_HANDLE_VALUE;
        }
        if (m_event) {
            CloseHandle(m_event);
            m_event = nullptr;
        }
    }

    bool waitIo(OVERLAPPED& ov, DWORD* transferred, int timeoutMs)
    {
        if (WaitForSingleObject(ov.hEvent, DWORD(timeoutMs)) != WAIT_OBJECT_0) {
            CancelIoEx(m_handle, &ov);
            GetOverlappedResult(m_handle, &ov, transferred, TRUE);
            return false;
        }
        return GetOverlappedResult(m_handle, &ov, transferred, FALSE);
    }

    bool writeReport(const QByteArray& report)
    {
        QByteArray buf = report.left(m_outputLength);
        buf.resize(m_outputLength, '\0');
        OVERLAPPED ov{};
        ov.hEvent = m_event;
        ResetEvent(m_event);
        DWORD written = 0;
        if (!WriteFile(m_handle, buf.constData(), DWORD(buf.size()), &written, &ov)
            && GetLastError() != ERROR_IO_PENDING)
            return false;
        return waitIo(ov, &written, 1000) && int(written) == buf.size();
    }

    // Input reports carry a leading report-id byte (always 0 here); it is stripped.
    QByteArray readReport(int timeoutMs)
    {
        QByteArray buf(m_inputLength, '\0');
        OVERLAPPED ov{};
        ov.hEvent = m_event;
        ResetEvent(m_event);
        DWORD read = 0;
        if (!ReadFile(m_handle, buf.data(), DWORD(buf.size()), &read, &ov) && GetLastError() != ERROR_IO_PENDING)
            return {};
        if (!waitIo(ov, &read, timeoutMs) || read <= 1)
            return {};
        return buf.mid(1, int(read) - 1);
    }

    static QByteArray build(quint8 cmd, const QByteArray& payload, int size)
    {
        QByteArray buf(size + 1, '\0');
        buf[1] = char(0x5A);
        buf[2] = char(0xA5);
        buf[3] = char(cmd);
        buf[4] = char(2 + payload.size());
        for (int i = 0; i < payload.size(); ++i)
            buf[5 + i] = payload[i];
        quint8 sum = 0;
        for (int i = 3; i < 3 + 2 + payload.size(); ++i)
            sum += quint8(buf[i]);
        buf[3 + 2 + payload.size()] = char(sum);
        return buf;
    }

    // Replies go to every open handle, so Space Station's own traffic shows up here too.
    bool command(quint8 cmd, const QByteArray& payload, int size, int timeoutMs)
    {
        while (!readReport(0).isEmpty()) {
        }
        if (!writeReport(build(cmd, payload, size)))
            return false;
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < timeoutMs) {
            const QByteArray r = readReport(int(timeoutMs - t.elapsed()));
            if (r.size() > 3 && quint8(r[0]) == 0x5A && quint8(r[1]) == 0xA5 && quint8(r[2]) == cmd)
                return true;
        }
        return false;
    }

    bool doUpload(int id, const QByteArray& rgb, int frameCount, int period, int brightness, QString* error)
    {
        if (frameCount < 1 || frameCount > 255 || rgb.size() != frameCount * kLedCount * 3) {
            *error = QCoreApplication::translate("HidDock", "Invalid animation data.");
            return false;
        }
        if (!open(error))
            return false;

        QByteArray blob;
        blob.reserve(6 + rgb.size());
        blob.append(char(frameCount));
        blob.append(char(std::clamp(period, 1, 255)));
        blob.append(char(std::clamp(brightness, 1, 100)));
        blob.append(char(kModeCustom));
        blob.append(char(0));   // direction
        blob.append(char(0));   // palette size
        blob.append(rgb);

        const int total = (int(blob.size()) + kPackBytes - 1) / kPackBytes;
        QByteArray start;
        start.append(char(10)).append(char(0)).append(char(0)).append(char(total >> 8)).append(char(total & 0xFF));
        if (!command(kCmdLedStart, start, 32, 1000)) {
            *error = QCoreApplication::translate("HidDock", "The dock did not accept the upload.");
            return false;
        }

        for (int index = 0; index < total; ++index) {
            const QByteArray chunk = blob.mid(index * kPackBytes, kPackBytes);
            QByteArray payload;
            payload.append(char(total >> 8)).append(char(total & 0xFF));
            payload.append(char(index >> 8)).append(char(index & 0xFF));
            payload.append(char(chunk.size()));
            payload.append(chunk);
            bool acked = false;
            for (int attempt = 0; attempt < 3 && !acked; ++attempt)
                acked = command(kCmdLedPack, payload, 64, 600);
            if (!acked) {
                *error = QCoreApplication::translate("HidDock", "The dock stopped responding at packet %1 of %2. "
                                                                "Send the animation again.")
                             .arg(index + 1)
                             .arg(total);
                return false;
            }
            if (index % 8 == 0 || index + 1 == total)
                emit uploadProgress(id, double(index + 1) / total);
        }
        return true;
    }

    HANDLE m_handle = INVALID_HANDLE_VALUE;
    HANDLE m_event = nullptr;
    int m_outputLength = 65;
    int m_inputLength = 65;
    bool m_present = false;
};

HidDock::HidDock(QObject* parent) : QObject(parent), m_thread(new QThread(this)), m_worker(new HidDockWorker)
{
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &HidDockWorker::presenceChanged, this, [this](bool present) {
        m_present = present;
        emit presenceChanged(present);
    });
    connect(m_worker, &HidDockWorker::uploadProgress, this, &HidDock::uploadProgress);
    connect(m_worker, &HidDockWorker::uploadFinished, this, &HidDock::uploadFinished);
    m_thread->start();
}

HidDock::~HidDock()
{
    m_thread->quit();
    m_thread->wait();
}

void HidDock::start()
{
    QMetaObject::invokeMethod(m_worker, [w = m_worker] {
        auto* timer = new QTimer(w);
        timer->setInterval(2000);
        QObject::connect(timer, &QTimer::timeout, w, &HidDockWorker::poll);
        timer->start();
        w->poll();
    });
}

int HidDock::upload(const QByteArray& rgb, int frameCount, int period, int brightness)
{
    const int id = m_nextId++;
    QMetaObject::invokeMethod(m_worker, [=, w = m_worker] { w->upload(id, rgb, frameCount, period, brightness); });
    return id;
}

} // namespace fg

#include "HidDock.moc"
