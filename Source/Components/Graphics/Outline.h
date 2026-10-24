#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Math/Vector3.h"

// A coloured silhouette around the mesh (or compact model part) on this entity: the mesh is drawn
// again inflated along its normals with the opposite cull (an inverted hull), after the opaque
// scene and sky, so the outline respects occlusion, skinning and vertex animation. On a compact
// model's root the outline applies to every part.
class Outline
    : public Component<Outline>
{
    ME_REFLECTABLE( Outline )
public:
    Outline();

    Vector3 Color = Vector3( 1.f, 0.6f, 0.1f );
    // Metres the hull inflates by; how thick the ring reads depends on distance.
    float Width = 0.02f;
};

ME_REGISTER_COMPONENT_FOLDER( Outline, "Rendering" )
