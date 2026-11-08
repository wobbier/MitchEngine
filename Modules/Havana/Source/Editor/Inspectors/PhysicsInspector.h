#pragma once
#include "Dementia.h"

#if USING( ME_EDITOR )

// The physics inspector widgets (UX-8), as ReflectionUI field decorators: a mass ladder of
// everyday things under Rigidbody(2D).Mass, velocity decay graphs under the damping fields, axis
// lock toggles under the Rigidbody's lock fields (and Rigidbody2D.FreezeRotation), and on every
// collider a bouncing ball under Restitution and a ramp at the slide angle under Friction.
namespace PhysicsInspector
{
    void Register();
}

#endif
