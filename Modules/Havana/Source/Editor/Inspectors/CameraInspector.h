#pragma once
#include "Dementia.h"

#if USING( ME_EDITOR )

// The camera and lens inspector widgets (UX-6), as ReflectionUI field decorators: the focal length
// and a frustum wedge with a lens ring under Camera.FieldOfView, the internal resolution under
// Camera.RenderScale, an iris dial under PostProcess.DoFAperture and the sharp band on a focus bar
// under PostProcess.DoFFocusDistance.
namespace CameraInspector
{
    void Register();
}

#endif
