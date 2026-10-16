#pragma once

#include "core/Effect.h"

namespace fg {

class LedLayout;
class ShaderRenderer;

/**
 * A Shadertoy-style GLSL mainImage rendered on the GPU and averaged over each LED's cell.
 * Shaders animated by iTime do not close the loop by themselves, so its end crossfades into the start.
 */
class ShaderEffect : public Effect {
public:
    static QString defaultSource();

    QString typeId() const override { return QStringLiteral("shader"); }
    QString name() const override;
    QString category() const override;
    QList<ParamDesc> params() const override;

    void render(const RenderContext& ctx, Frame& out) const override;
    bool listedInLibrary() const override { return false; }
    std::unique_ptr<Effect> clone() const override;

    QJsonObject toJson() const override;
    void fromJson(const QJsonObject& o) override;

    const QString& source() const { return m_source; }
    void setSource(const QString& source) { m_source = source; }

private:
    bool sample(ShaderRenderer& renderer, const LedLayout& layout, double time, const RenderContext& ctx,
                std::vector<Rgb>& out) const;

    QString m_source = defaultSource();
};

} // namespace fg
