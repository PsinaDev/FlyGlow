#pragma once

#include "Effect.h"

namespace fg {

/**
 * Painted keyframes interpolated per LED in OKLab. The timeline is cyclic: the last key blends back
 * into the first. Always holds at least one key.
 */
class KeyframeEffect : public Effect {
public:
    enum class Easing { Smooth = 0, Linear = 1, Hold = 2 };

    struct Keyframe {
        double time = 0;                 /**< phase 0..1 */
        std::vector<Rgba> colors;        /**< one per LED of LedLayout::cd2(), linear light */
        Easing easing = Easing::Smooth;  /**< curve of the segment that starts here */
    };

    KeyframeEffect();

    QString typeId() const override { return "keyframes"; }
    QString name() const override { return QT_TRANSLATE_NOOP("Effects", "Custom animation"); }
    QString category() const override { return QT_TRANSLATE_NOOP("Effects", "Drawing"); }
    QList<ParamDesc> params() const override { return {}; }

    void render(const RenderContext& ctx, Frame& out) const override;
    bool isAnimated() const override { return m_keys.size() > 1; }
    std::unique_ptr<Effect> clone() const override;

    QJsonObject toJson() const override;
    void fromJson(const QJsonObject& o) override;

    int keyCount() const { return int(m_keys.size()); }
    const Keyframe& key(int i) const { return m_keys[size_t(i)]; }
    const std::vector<Keyframe>& keys() const { return m_keys; }

    /** Index of the key within tolerance of time on the phase circle, or -1. */
    int keyAt(double time, double tolerance = 0.004) const;
    /**
     * Inserts a key holding whatever the animation shows at time now.
     * @return Its index, or the index of a key already there.
     */
    int addKey(double time);
    /** Never removes the last remaining key. */
    void removeKey(int index);
    /**
     * Moves a key, keeping keys sorted; refuses to land on another key.
     * @return The key's new index.
     */
    int moveKey(int index, double time);
    void setEasing(int index, Easing e);

    void setLed(int key, int led, const Rgba& c);
    void fill(int key, const Rgba& c);

private:
    void sortKeys();
    std::vector<Keyframe> m_keys;
};

} // namespace fg
