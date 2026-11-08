#pragma once
#include "Dementia.h"

#if USING( ME_EDITOR )

// The AudioSource inspector widgets (UX-9), as ReflectionUI field decorators: a level meter under
// Volume (with the live level while it plays), a keyboard under Pitch, a 2D / 3D slider under
// Spatial Blend and the distance falloff graph (with Min / Max Distance handles and the rolloff
// names) under Rolloff.
namespace AudioInspector
{
    void Register();
}

#endif
