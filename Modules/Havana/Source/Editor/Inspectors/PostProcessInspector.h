#pragma once
#include "Dementia.h"

#if USING( ME_EDITOR )

// The post-processing and grading inspector widgets (UX-7), as ReflectionUI field decorators: an
// EV ladder under PostProcess.ExposureCompensation, the tonemap curve under Tonemapper, a white
// balance pad under Tint, the lift / gamma / gain curve under Gain and the fog's visibility under
// FogDensity.
namespace PostProcessInspector
{
    void Register();
}

#endif
