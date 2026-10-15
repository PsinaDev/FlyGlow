#pragma once

#include "media/LedSampler.h"
#include "media/MediaDecoder.h"

#include <QDialog>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <memory>
#include <vector>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;
class QWidget;

namespace fg {

struct Layer;
class DockView;
class MediaEffect;
class SliderRow;
class ToggleSwitch;

namespace detail {
class MediaSourcePreview;
}

class MediaImportDialog : public QDialog {
    Q_OBJECT
public:
    /**
     * Asks for a file, then shows the dialog. Returns a MediaEffect layer named after the file,
     * or nullptr if cancelled. suggestedLoopSeconds (may be nullptr) receives the clip length
     * when "Match loop length to clip" is on, else 0.
     */
    static std::unique_ptr<Layer> run(QWidget* parent, double* suggestedLoopSeconds);

    ~MediaImportDialog() override;

private:
    struct DecodeJob;

    MediaImportDialog(const QString& path, QWidget* parent);

    QWidget* buildControls();
    void startDecode();
    void onDecodeFinished();
    void scheduleDecode();
    void scheduleResample();
    void resample();
    void tick();
    void showFrameAt(double clipSeconds);
    void setupTrimRanges(double duration);
    void updateInfo();
    void updateButtons();
    void setZoom(double zoom);
    std::unique_ptr<Layer> takeLayer() const;

    QString m_path;
    media::DecodedClip m_clip;
    bool m_rangesReady = false;
    double m_sourceDuration = 0;
    media::Framing m_framing;
    media::Adjustments m_adjust;
    media::LedSampler m_sampler;
    std::unique_ptr<MediaEffect> m_effect;
    std::vector<double> m_frameStarts;

    std::shared_ptr<DecodeJob> m_job;
    QFutureWatcher<media::DecodedClip> m_watcher;
    bool m_restartDecode = false;

    QTimer* m_decodeTimer = nullptr;
    QTimer* m_resampleTimer = nullptr;
    QTimer* m_playTimer = nullptr;
    QTimer* m_progressTimer = nullptr;
    QElapsedTimer m_clock;
    int m_shownFrame = -1;

    detail::MediaSourcePreview* m_source = nullptr;
    DockView* m_dock = nullptr;
    QComboBox* m_fit = nullptr;
    SliderRow* m_zoom = nullptr;
    QComboBox* m_rotation = nullptr;
    ToggleSwitch* m_flip = nullptr;
    SliderRow* m_trimStart = nullptr;
    SliderRow* m_trimEnd = nullptr;
    SliderRow* m_maxFrames = nullptr;
    SliderRow* m_brightness = nullptr;
    SliderRow* m_saturation = nullptr;
    ToggleSwitch* m_smooth = nullptr;
    ToggleSwitch* m_matchLoop = nullptr;
    QList<QWidget*> m_clipControls;
    QWidget* m_progressRow = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_progressLabel = nullptr;
    QLabel* m_info = nullptr;
    QLabel* m_sourceInfo = nullptr;
    QPushButton* m_import = nullptr;
};

} // namespace fg
