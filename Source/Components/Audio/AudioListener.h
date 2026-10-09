#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"

// Where the game hears from (usually on the camera or the player). The first active listener wins;
// without one the current camera hears, and in edit mode the editor camera does.
class AudioListener
    : public Component<AudioListener>
{
    ME_REFLECTABLE( AudioListener )
public:
    AudioListener();
};

ME_REGISTER_COMPONENT_FOLDER( AudioListener, "Audio" )
