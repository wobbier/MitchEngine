#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Math/Vector3.h"
#include "RenderCommands.h"
#include <Utils/HavanaUtils.h>

class DirectionalLight
	: public Component<DirectionalLight>
{
public:
	DirectionalLight()
		: Component("DirectionalLight")
		, Direction(0.25f, 0.5f, -1.0f)
		, Ambient(0.2f, 0.2f, 0.2f)
		, Diffuse(1.0f, 1.0f, 1.0f)
	{

	}
	~DirectionalLight() = default;

	// Separate init from construction code.
	virtual void Init() final
	{
	}

	Vector3 Direction;
	Vector3 Ambient;
	Vector3 Diffuse;

#if USING( ME_EDITOR )
	virtual void OnEditorInspect() final
	{
		ImGui::DragFloat4("Direction", &Direction.x);
		bool hdr = true;
		int misc_flags = (hdr ? ImGuiColorEditFlags_HDR : 0) | ImGuiColorEditFlags_NoDragDrop;

		HavanaUtils::ColorButton("Ambient", Ambient);
		HavanaUtils::ColorButton("Diffuse", Diffuse);

		if (ImGui::Button("Look At World 0"))
		{
			Transform& transform = Parent->GetComponent<Transform>();
			Direction = (Vector3() - transform.GetWorldPosition()).Normalized();
		}
	}
#endif

private:
	virtual void OnSerialize(json& outJson) final
	{
		outJson["Direction"] = { Direction.x, Direction.y, Direction.z };
		outJson["Ambient"] = { Ambient.x, Ambient.y, Ambient.z };
		outJson["Diffuse"] = { Diffuse.x, Diffuse.y, Diffuse.z };
	}

	virtual void OnDeserialize(const json& inJson) final
	{
		if (inJson.contains("Direction"))
		{
			Direction = { (float)inJson["Direction"][0], (float)inJson["Direction"][1], (float)inJson["Direction"][2] };
		}
		if (inJson.contains("Ambient"))
		{
			Ambient = { (float)inJson["Ambient"][0], (float)inJson["Ambient"][1], (float)inJson["Ambient"][2] };
		}
		if (inJson.contains("Diffuse"))
		{
			Diffuse = { (float)inJson["Diffuse"][0], (float)inJson["Diffuse"][1], (float)inJson["Diffuse"][2] };
		}
	}
};
ME_REGISTER_COMPONENT_FOLDER(DirectionalLight, "Rendering")
