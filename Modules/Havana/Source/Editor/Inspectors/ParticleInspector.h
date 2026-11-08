#pragma once
#include "Dementia.h"

#if USING( ME_EDITOR )

// The ParticleSystem's inspector widgets (UX-15), as ReflectionUI field decorators: a "Color Over
// Lifetime" gradient under End Alpha (two fixed stops: Start Color + Start Alpha at birth, End
// Color + End Alpha at death, with Start Color 2 as a band under the bar) and a "Size Over
// Lifetime" curve under End Size (from 1 at birth, a locked key, straight to End Size). The
// data stays the reflected fields: the editors write them back.
namespace ParticleInspector
{
    void Register();
}

#endif
