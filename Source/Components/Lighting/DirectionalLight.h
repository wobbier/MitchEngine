#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Math/Vector3.h"
#include "RenderCommands.h"

class DirectionalLight : public Component<DirectionalLight>
{
public:

	DirectionalLight() = default;
	~DirectionalLight() = default;

	// Separate init from construction code.
	virtual void Init() final
	{
		// Prepare the constant buffer to send it to the graphics device.
		Direction = DirectX::XMFLOAT4(0.25f, 0.5f, -1.0f, 1.0f);
		Ambient = DirectX::XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
		Diffuse = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	DirectX::XMFLOAT4 Direction;
	DirectX::XMFLOAT4 Ambient;
	DirectX::XMFLOAT4 Diffuse;
#if ME_EDITOR
	virtual void OnEditorInspect() final
	{
		ImGui::DragFloat4("Direction", &Direction.x);
		bool hdr = true;
		int misc_flags = (hdr ? ImGuiColorEditFlags_HDR : 0) | ImGuiColorEditFlags_NoDragDrop;

		ImGui::ColorEdit4("Ambient", (float*)&Ambient.x, ImGuiColorEditFlags_Float | misc_flags);
		ImGui::ColorEdit4("Diffuse", (float*)&Diffuse.x, ImGuiColorEditFlags_Float | misc_flags);
	}
#endif
};
ME_REGISTER_COMPONENT(DirectionalLight)
