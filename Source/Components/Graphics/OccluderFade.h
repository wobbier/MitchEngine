#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include <string>

// On a follow camera (ME-279): whatever sits between it and Target dithers away, so walls and
// pillars don't hide the player in third-person views. Each frame rays go from the camera to the
// target (the line, and four more Radius around it); meshes on the entities they hit (and on those
// entities' children) fade to Amount over FadeSeconds, and back when they're clear.
class OccluderFade
    : public Component<OccluderFade>
{
    ME_REFLECTABLE( OccluderFade )
public:
    OccluderFade();

    std::string Target = "Player";  // the entity, by name, the camera keeps in view
    float Radius = 0.3f;            // metres around the line that count as in the way
    float Amount = 0.8f;            // how much of an occluder dithers away (0..1)
    float FadeSeconds = 0.2f;
};
ME_REGISTER_COMPONENT_FOLDER( OccluderFade, "Rendering" )
