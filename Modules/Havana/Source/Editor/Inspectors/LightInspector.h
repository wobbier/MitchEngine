#pragma once
#include "Dementia.h"

#if USING( ME_EDITOR )

// The Light component's inspector widgets (UX-5), as ReflectionUI field decorators: a colour
// temperature bar under Color, reference rungs under Intensity, the beam's cone under the cone
// angles (spot and rectangle lights) and the falloff curve under Range (local lights).
namespace LightInspector
{
    void Register();
}

#endif
