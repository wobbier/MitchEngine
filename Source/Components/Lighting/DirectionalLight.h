#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Math/Vector3.h"
#include "RenderCommands.h"

class DirectionalLight
	: public Component<DirectionalLight>
{
public:
	DirectionalLight()
		: Direction(0.25f, 0.5f, -1.0f, 1.0f)
		, Ambient(0.2f, 0.2f, 0.2f, 1.0f)
		, Diffuse(1.0f, 1.0f, 1.0f, 1.0f)
	{

	}
	~DirectionalLight() = default;

	// Separate init from construction code.
	virtual void Init() final
	{
	}

	DirectX::XMFLOAT4 Direction = DirectX::XMFLOAT4();
	DirectX::XMFLOAT4 Ambient;
	DirectX::XMFLOAT4 Diffuse;

#if ME_EDITOR
	virtual void OnEditorInspect() final
	{
		ImGui::DragFloat4("Direction", &Direction.x);
		bool hdr = true;
		int misc_flags = (hdr ? ImGuiColorEditFlags_HDR : 0) | ImGuiColorEditFlags_NoDragDrop;

		ImGui::ColorEdit4("Ambient", (float*)& Ambient.x, ImGuiColorEditFlags_Float | misc_flags);
		ImGui::ColorEdit4("Diffuse", (float*)& Diffuse.x, ImGuiColorEditFlags_Float | misc_flags);
	}
#endif

	virtual void Serialize(json& outJson) override
	{
		Component::Serialize(outJson);

		outJson["Direction"] = { Direction.x, Direction.y, Direction.z };
		outJson["Ambient"] = { Ambient.x, Ambient.y, Ambient.z };
		outJson["Diffuse"] = { Diffuse.x, Diffuse.y, Diffuse.z };
	}


	virtual void Deserialize(const json& inJson) override
	{
		if (inJson.contains("Direction"))
		{
			Direction = { (float)inJson["Direction"][0], (float)inJson["Direction"][1], (float)inJson["Direction"][2], 1.f };
		}
		if (inJson.contains("Ambient"))
		{
			Ambient = { (float)inJson["Ambient"][0], (float)inJson["Ambient"][1], (float)inJson["Ambient"][2], 1.f };
		}
		if (inJson.contains("Diffuse"))
		{
			Diffuse = { (float)inJson["Diffuse"][0], (float)inJson["Diffuse"][1], (float)inJson["Diffuse"][2], 1.f };
		}
	}

};
ME_REGISTER_COMPONENT(DirectionalLight)
