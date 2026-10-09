#include <Utils/CommonUtils.h>
#include <optick.h>
#include <Engine/Engine.h>
#include <Engine/World.h>
#include "World/SceneSerializer.h"
#include "Editor/EditorOperations.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstring>

#if USING( ME_EDITOR )

namespace
{
	bool ContainsInsensitive(const std::string& haystack, const char* needle)
	{
		if (!needle || needle[0] == '\0')
		{
			return true;
		}
		auto it = std::search(haystack.begin(), haystack.end(), needle, needle + std::strlen(needle), [](char a, char b) {
			return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
		});
		return it != haystack.end();
	}
}


bool CommonUtils::DrawAddComponentList(const EntityHandle& entity, char* filterBuffer, size_t filterBufferSize)
{
	static char s_filter[128] = {};
	if (!filterBuffer)
	{
		filterBuffer = s_filter;
		filterBufferSize = sizeof(s_filter);
	}
	if (!entity)
	{
		return false;
	}

	if (ImGui::IsWindowAppearing())
	{
		filterBuffer[0] = '\0';
		ImGui::SetKeyboardFocusHere();
	}
	ImGui::SetNextItemWidth(-1.f);
	const bool submitted = ImGui::InputTextWithHint("##AddComponentFilter", "Search components", filterBuffer, filterBufferSize, ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::Separator();

	// Folder -> components, skipping ones the entity already has.
	std::map<std::string, std::vector<std::string>> byFolder;
	for (auto& [name, info] : GetComponentRegistry())
	{
		if (entity->GetComponentByName(name) || !ContainsInsensitive(name, filterBuffer))
		{
			continue;
		}
		byFolder[info.Folder].push_back(name);
	}

	std::string chosen;
	size_t shown = 0;
	ImGui::BeginChild("##AddComponentList", ImVec2(260.f, 320.f));
	for (auto& [folder, names] : byFolder)
	{
		if (!folder.empty())
		{
			ImGui::TextDisabled("%s", folder.c_str());
		}
		for (const std::string& name : names)
		{
			if (ImGui::Selectable(name.c_str()))
			{
				chosen = name;
			}
			if (submitted && shown == 0 && chosen.empty())
			{
				// Enter picks the first match.
				chosen = name;
			}
			++shown;
		}
	}
	if (shown == 0)
	{
		ImGui::TextDisabled("No matching components");
	}
	ImGui::EndChild();

	if (!chosen.empty())
	{
		EditorOps::AddComponent(*entity.Get(), chosen);
		ImGui::CloseCurrentPopup();
		return true;
	}
	return false;
}


void CommonUtils::SerializeEntity(json& outEntity, Transform* CurrentTransform)
{
	OPTICK_EVENT("CommonUtils::SerializeEntity");
	World& world = *GetEngine().GetWorld().lock();
	outEntity = SceneSerializer::SerializeEntities(world, { CurrentTransform->Parent.Get() });
}

#endif
