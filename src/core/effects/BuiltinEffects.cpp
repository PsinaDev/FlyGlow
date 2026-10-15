#include "core/Effect.h"
#include "EffectsInternal.h"

namespace fg {

void registerBuiltinEffects()
{
    static bool registered = false;
    if (registered)
        return;
    registered = true;
    // Registration order is the library's display order.
    EffectRegistry& r = EffectRegistry::instance();
    registerBasicEffects(r);
    registerWaveEffects(r);
    registerNatureEffects(r);
    registerParticleEffects(r);
}

} // namespace fg
