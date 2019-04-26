#include "PCH.h"
#include "Light.h"
#include "Havana.h"

void Light::Init()
{
	cmd.diffuse = { Colour.GetInternalVec()[0],Colour.GetInternalVec()[1],Colour.GetInternalVec()[2], 1.f };
	Game::GetEngine().GetRenderer().PushLight(cmd);
}

#if ME_EDITOR

void Light::OnEditorInspect()
{
	Havana::EditableVector3("Color", Colour);
}

#endif