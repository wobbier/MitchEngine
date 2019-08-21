#include "PCH.h"
#include "FlyingCamera.h"
#include "imgui.h"

FlyingCamera::FlyingCamera()
	: Component("FlyingCamera")
{

}

FlyingCamera::~FlyingCamera()
{

}

void FlyingCamera::Init()
{

}
#if ME_EDITOR

void FlyingCamera::OnEditorInspect()
{
	ImGui::DragFloat("Flying Speed", &FlyingSpeed);
	ImGui::DragFloat("Speed Modifier", &SpeedModifier);
	ImGui::DragFloat("Look Sensitivity", &LookSensitivity, 0.01f);
}

#endif