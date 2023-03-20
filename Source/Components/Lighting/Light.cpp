#include "PCH.h"
#include "Light.h"
#include "Engine/Engine.h"
#include "Utils/HavanaUtils.h"

Light::Light()
	: Component("Light")
{
}

void Light::Init()
{
	//cmd.diffuse = { Colour[0], Colour[1], Colour[2], 1.f };
	//GetEngine().GetRenderer().PushLight(cmd);
}

void Light::OnSerialize(json& outJson)
{
}

void Light::OnDeserialize(const json& inJson)
{
}

#if USING( ME_EDITOR )

void Light::OnEditorInspect()
{
	HavanaUtils::EditableVector3("Color", Colour);
}

#endif