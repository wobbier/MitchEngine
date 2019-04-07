#include "PCH.h"
#include "Light.h"
#include "Havana.h"

void Light::Init()
{
}

#if ME_EDITOR

void Light::OnEditorInspect()
{
	Havana::EditableVector3("Color", Colour);
}

#endif