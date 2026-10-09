#include "PCH.h"
#include "Light.h"
#include "Engine/Engine.h"
#include "Utils/HavanaUtils.h"

ME_REFLECT_BEGIN( Light )
    ME_FIELD_NAMED( Colour, "Color" ).Color();
ME_REFLECT_END()


Light::Light()
    : Component( "Light" )
{
}

void Light::Init()
{
    //cmd.diffuse = { Colour[0], Colour[1], Colour[2], 1.f };
    //GetEngine().GetRenderer().PushLight(cmd);
}

