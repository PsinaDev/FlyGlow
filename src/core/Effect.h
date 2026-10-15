#pragma once

#include "Color.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <functional>
#include <memory>
#include <vector>

namespace fg {

class LedLayout;

/** One colour per LED in linear light with straight alpha; a = 0 leaves the LED to the layers below. */
using Frame = std::vector<Rgba>;

/**
 * Display strings (label, choices) are English sources in the "Effects" translation context;
 * translate them with QCoreApplication::translate("Effects", ...) when shown.
 */
struct ParamDesc {
    enum class Type {
        Float,     /**< double in [min, max] */
        Int,       /**< int in [min, max] */
        Bool,
        Color,     /**< sRGB QColor */
        Gradient,  /**< fg::ColorGradient */
        Choice,    /**< int index into choices */
        Angle,     /**< double, degrees 0..360; 0 points right, 90 points down */
    };

    QString id;
    QString label;
    Type type = Type::Float;
    QVariant def;
    double min = 0;
    double max = 1;
    double step = 0.01;
    QStringList choices;
    QString suffix;
};

struct RenderContext {
    /** Position in the loop, [0, 1). */
    double phase = 0;
    /** Loop length in seconds. */
    double loopSeconds = 4;
    /** nullptr renders for LedLayout::cd2(). */
    const LedLayout* layout = nullptr;
};

/**
 * A pure function of loop phase: the same phase always gives the same frame, and
 * render(0) must match render(phase -> 1) so loops are seamless.
 */
class Effect {
public:
    virtual ~Effect() = default;

    /** Stable id stored in saved files. */
    virtual QString typeId() const = 0;
    /** English source text in the "Effects" translation context. */
    virtual QString name() const = 0;
    /** English source text in the "Effects" translation context. */
    virtual QString category() const = 0;
    virtual QList<ParamDesc> params() const = 0;

    /** Fills out for ctx.phase; out is already sized to the layout's LED count. */
    virtual void render(const RenderContext& ctx, Frame& out) const = 0;

    /** False when the output never changes with phase, so a single frame is baked. */
    virtual bool isAnimated() const { return true; }

    virtual bool listedInLibrary() const { return true; }

    /**
     * Deep copy. The default copies only parameter values, so effects with extra state override it.
     * @return nullptr if the type is not registered.
     */
    virtual std::unique_ptr<Effect> clone() const;

    /** Stored value, else the declared default, else an invalid QVariant. */
    QVariant value(const QString& id) const;
    void setValue(const QString& id, const QVariant& v);
    const QVariantMap& values() const { return m_values; }

    /** {"type": typeId(), "params": {...}}; overrides add their own keys. */
    virtual QJsonObject toJson() const;
    virtual void fromJson(const QJsonObject& o);

    /** @return nullptr for an unknown type. */
    static std::unique_ptr<Effect> createFromJson(const QJsonObject& o);

protected:
    double f(const QString& id) const { return value(id).toDouble(); }
    int i(const QString& id) const { return value(id).toInt(); }
    bool b(const QString& id) const { return value(id).toBool(); }
    QColor qc(const QString& id) const { return value(id).value<QColor>(); }
    /** Linear-light value of a Color parameter. */
    Rgb c(const QString& id) const { return color::fromQColor(qc(id)); }
    ColorGradient g(const QString& id) const { return ColorGradient::fromVariant(value(id)); }

    QVariantMap m_values;
};

class EffectRegistry {
public:
    struct Entry {
        QString typeId;
        QString name;
        QString category;
        std::function<std::unique_ptr<Effect>()> make;
        bool listed = true;
    };

    static EffectRegistry& instance();

    template <class T>
    void add()
    {
        T probe;
        m_entries.push_back({probe.typeId(), probe.name(), probe.category(),
                             [] { return std::unique_ptr<Effect>(new T()); }, probe.listedInLibrary()});
    }

    /** In registration order, which is the library's display order. */
    const QList<Entry>& entries() const { return m_entries; }
    /** @return nullptr for an unknown typeId. */
    std::unique_ptr<Effect> create(const QString& typeId) const;

private:
    QList<Entry> m_entries;
};

/** Idempotent; call once before creating effects or loading scenes. */
void registerBuiltinEffects();

namespace fx {
/** Deterministic hash of (seed, index) in [0, 1). */
double hash01(uint32_t seed, uint32_t index);
/** Smoothstep from e0 to e1; e1 < e0 gives a falling edge. */
double smooth(double e0, double e1, double x);
double wrap01(double x);
/** Shortest signed a - b on the phase circle, in [-0.5, 0.5). */
double phaseDelta(double a, double b);
/**
 * 2-D value noise that loops seamlessly in phase.
 * @param x, y        Layout units.
 * @param phase       [0, 1).
 * @param timeRadius  Radius of the time circle; larger morphs faster.
 * @return Roughly [0, 1].
 */
double loopNoise(double x, double y, double phase, uint32_t seed, double timeRadius = 1.0);
} // namespace fx

} // namespace fg
