#pragma once

#include <QByteArray>
#include <QObject>

class QThread;

namespace fg {

class HidDockWorker;

/**
 * The CD2 dock driven directly over its vendor HID interface (37D7:6001).
 * The handle is opened shared, so it coexists with Space Station's service holding the same device.
 * @note All device IO runs on a private thread; signals are delivered on the owner's thread.
 */
class HidDock : public QObject {
    Q_OBJECT
public:
    explicit HidDock(QObject* parent = nullptr);
    ~HidDock() override;

    /** Starts watching for the dock; presenceChanged() reports the first result. */
    void start();
    bool isPresent() const { return m_present; }

    /**
     * Uploads a Custom lighting config.
     *
     * @param rgb         frameCount * 162 * 3 bytes, LEDs in row-major order.
     * @param period      Frame interval in 20 ms units, 1..255.
     * @param brightness  1..100.
     * @return Request id echoed by uploadProgress() and uploadFinished().
     */
    int upload(const QByteArray& rgb, int frameCount, int period, int brightness);

signals:
    void presenceChanged(bool present);
    void uploadProgress(int requestId, double fraction);
    void uploadFinished(int requestId, bool ok, const QString& message);

private:
    QThread* m_thread;
    HidDockWorker* m_worker;
    bool m_present = false;
    int m_nextId = 1;
};

} // namespace fg
