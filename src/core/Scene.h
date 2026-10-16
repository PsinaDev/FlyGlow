#pragma once

#include "Effect.h"

#include <QJsonObject>
#include <QString>
#include <memory>
#include <vector>

namespace fg {

class LedLayout;

enum class BlendMode { Normal = 0, Add, Screen, Multiply, Lighten };
/** English sources in the "Effects" translation context, in enum order. */
QStringList blendModeNames();

struct Layer {
    QString name;
    bool visible = true;
    double opacity = 1.0;
    BlendMode blend = BlendMode::Normal;
    std::unique_ptr<Effect> effect;
    std::vector<float> mask;   /**< empty = whole dock, else one weight 0..1 per LED */

    std::unique_ptr<Layer> clone() const;
    float maskAt(int led) const { return mask.empty() ? 1.f : mask[size_t(led)]; }
};

/** Layers composited bottom-up in linear light, plus loop and output settings: what a .flyglow file holds. */
class Scene {
public:
    Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    std::vector<std::unique_ptr<Layer>> layers;   /**< [0] is the bottom layer */
    double loopSeconds = 4.0;
    int brightness = 100;       /**< dock brightness 1..100 */
    double outputGamma = 2.2;   /**< matching kDockGamma keeps the preview's light ratios on the LEDs; 1 sends linear values */
    bool dither = true;         /**< temporal error diffusion across frames when baking */

    /** Composites at phase into out, resized to layout.count(); linear light. */
    void render(double phase, const LedLayout& layout, std::vector<Rgb>& out) const;
    bool isAnimated() const;

    QJsonObject toJson() const;
    /**
     * Layers with an unknown effect type are skipped rather than failing the file.
     * @param error Receives a translated message when o is not a FlyGlow scene.
     */
    bool fromJson(const QJsonObject& o, QString* error = nullptr);
    /** Deep copy through a JSON round trip. */
    void copyFrom(const Scene& other);
};

struct BakeResult {
    int frameCount = 0;
    int period = 1;              /**< frame interval in 20 ms units */
    double loopSeconds = 0;      /**< actual loop length after rounding to whole periods */
    QByteArray rgb;              /**< frameCount * LEDs * 3 bytes, 8-bit RGB */
};

/** Frame count and period bake() will use, without rendering anything. */
void bakeTiming(const Scene& scene, int maxFrames, int* frameCount, int* period);

/** @param maxFrames Clamped to 2..255. */
BakeResult bake(const Scene& scene, const LedLayout& layout, int maxFrames = 200);

/**
 * How the dock turns an output value into light: roughly (value / 255)^kDockGamma. Measured on firmware 0.0.3.9,
 * where values up to 8 leave the LEDs dark.
 */
inline constexpr double kDockGamma = 2.2;

/** Linear light to the unrounded 0..255 output value for the given output gamma. */
float encodeOutput(float linear, double gamma);

/**
 * One frame's 8-bit RGB values exactly as bake() sends them.
 * @param err  Rounding error per channel, carried in from the previous frame and handed on to the next;
 *             resized to fit and zeroed when it does not. nullptr rounds without dithering.
 * @param out  frame.size() * 3 bytes.
 */
void encodeFrame(const std::vector<Rgb>& frame, double gamma, std::vector<float>* err, uchar* out);

} // namespace fg
