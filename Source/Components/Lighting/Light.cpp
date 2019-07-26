#include "PCH.h"
#include "Light.h"
#include "Havana.h"
#include "Engine/Engine.h"
#include "Utils/HavanaUtils.h"

void Light::Init()
{
	cmd.diffuse = { Colour[0], Colour[1], Colour[2], 1.f };
	//GetEngine().GetRenderer().PushLight(cmd);
}

#if ME_EDITOR

void Light::OnEditorInspect()
{
	HavanaUtils::EditableVector3("Color", Colour);
}

#endif