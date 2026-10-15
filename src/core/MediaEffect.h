#pragma once

#include "Effect.h"

namespace fg {

class MediaEffect : public Effect {
public:
    static constexpr int kMaxFrames = 600;

    QString typeId() const override { return QStringLiteral("media"); }
    QString name() const override;
    QString category() const override;
    QList<ParamDesc> params() const override;

    void render(const RenderContext& ctx, Frame& out) const override;
    bool isAnimated() const override { return m_frames.size() > 1; }
    bool listedInLibrary() const override { return false; }
    std::unique_ptr<Effect> clone() const override;

    QJsonObject toJson() const override;
    void fromJson(const QJsonObject& o) override;

    /**
     * Frames hold one linear-light colour per LED; durations are seconds per frame.
     * Colours are rounded to 8-bit sRGB, as they would be when saved, and clips
     * longer than kMaxFrames are respaced evenly to that many frames.
     */
    void setClip(std::vector<std::vector<Rgb>> frames, std::vector<double> durations);

    int frameCount() const { return int(m_frames.size()); }
    const std::vector<Rgb>& frame(int index) const { return m_frames[size_t(index)]; }
    double frameDuration(int index) const { return m_durations[size_t(index)]; }
    double clipSeconds() const { return m_total; }

private:
    void rebuildTimeline();

    std::vector<std::vector<Rgb>> m_frames;
    std::vector<double> m_durations;
    std::vector<double> m_starts;
    double m_total = 0;
};

} // namespace fg
