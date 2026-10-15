#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

class QLocalSocket;

namespace fg {

/**
 * Uploads to the CD2 through Space Station's own service on `\\.\pipe\fcs.sock`; see docs/ipc-protocol.md.
 * @note The service serves one pipe client at a time, so the pipe is held only while there is work. While
 *       Space Station's window is open or hidden in the tray, the service cannot be reached.
 * @note `FLYGLOW_FDG_PIPE=name` switches to `\\.\pipe\name`, e.g. the fake service in tools/fdg_fake_service.py.
 */
class SpaceStationClient : public QObject {
    Q_OBJECT
public:
    struct DockInfo {
        QString uid;
        QString name;       /**< nickname or model name, for display */
        QString firmware;   /**< empty unless the service reports it */
    };

    explicit SpaceStationClient(QObject* parent = nullptr);
    ~SpaceStationClient() override;

    /**
     * Queries devices now and every 30 s after; failed attempts are retried with backoff
     * (1 s doubling to 30 s) while there is work to do.
     */
    void start();
    /** True when the most recent connection attempt reached the service; the pipe itself is released when idle. */
    bool isConnected() const;

    /**
     * Connected CD2 docks the service reports; emptied when the service becomes unreachable.
     * devicesChanged() fires when the list changes and once when it first becomes known.
     */
    QList<DockInfo> docks() const;
    void refreshDevices();

    /**
     * Queues a Custom animation upload to the dock uid. Uploads are sent one at a time; a newer upload
     * for the same dock replaces one still waiting, which then finishes with ok = false.
     *
     * @param rgb         frameCount * 162 * 3 bytes, LEDs in row-major order, 8-bit RGB.
     * @param frameCount  1..255; 200 is known to be safe.
     * @param period      Frame interval in 20 ms units, clamped to 1..255.
     * @param brightness  Clamped to 1..100.
     * @return Request id echoed by uploadProgress() and uploadFinished(), which fires once the service
     *         confirms the last HID pack reached the dock (about 0.1 s for 1 frame, 4.3 s for 50).
     */
    int uploadCustom(const QString& uid, const QByteArray& rgb, int frameCount, int period,
                     int brightness);

signals:
    void connectedChanged(bool connected);
    void devicesChanged();
    /** Only emitted if the service reports progress. */
    void uploadProgress(int requestId, double fraction);
    /** message is translated, for the user. */
    void uploadFinished(int requestId, bool ok, const QString& message);
    /** Diagnostic line for the log. */
    void logMessage(const QString& line);

private:
    struct Private;
    Private* d;
};

} // namespace fg
