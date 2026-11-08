#pragma once
#include "Dementia.h"

#if USING( ME_EDITOR )

// The character and navigation inspector widgets (UX-10), as ReflectionUI field decorators: on
// CharacterController(2D) a capsule silhouette beside a person under Height, a pace strip under
// MaxSpeed, the jump's arc under JumpHeight and a slope wedge under SlopeLimit; on NavMeshAgent
// the agent's cylinder under Height and a pace strip under Speed.
namespace CharacterInspector
{
    void Register();
}

#endif
