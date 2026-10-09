#include "PropertiesWidget.h"
#include <optick.h>
#include <ECS/Component.h>
#include <ECS/Core.h>
#include <ECS/Entity.h>
#include <Engine/Engine.h>
#include <Engine/World.h>
#include <Utils/CommonUtils.h>
#include "Editor/EditorComponentInfoCache.h"
#include "Editor/EditorOperations.h"
#include "Editor/Selection.h"
#include "World/SceneSerializer.h"
#include <Utils/HavanaUtils.h>
#include "UI/Colors.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

#if USING( ME_EDITOR )

namespace
{
	constexpr const char* kComponentClipboardPrefix = "MitchEngine.Component:";

	std::string EditKey(const Entity& entity, const BaseComponent& comp)
	{
		return std::to_string(entity.GetGUID()) + ":" + comp.GetName();
	}
}


PropertiesWidget::PropertiesWidget()
	: HavanaWidget("Properties")
{
}


void PropertiesWidget::Init()
{
}


void PropertiesWidget::Destroy()
{
	m_snapshots.clear();
	m_pendingEdits.clear();
}


void PropertiesWidget::Update()
{
}


void PropertiesWidget::SortComponents(std::vector<BaseComponent*>& components) const
{
	const EditorComponentCache::ComponentInfoMap& componentData = EditorComponentCache::GetAllComponentsInfo();
	auto order = [&componentData](const BaseComponent* comp) {
		auto it = componentData.find(comp->GetTypeId());
		return it != componentData.end() ? it->second.Order : EditorComponentCache::kDefaultSortingOrder;
	};
	std::stable_sort(components.begin(), components.end(), [&order](const BaseComponent* a, const BaseComponent* b) {
		return order(a) > order(b);
	});
}


void PropertiesWidget::Render()
{
	if (!IsOpen)
	{
		return;
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.f, 0.f });
	bool windowOpen = ImGui::Begin("Properties", &IsOpen);
	ImGui::PopStyleVar();
	if (!windowOpen)
	{
		ImGui::End();
		return;
	}

	Selection& selection = Selection::Get();
	if (m_selectionVersion != selection.GetVersion())
	{
		// New selection: forget snapshots of what was inspected before.
		m_selectionVersion = selection.GetVersion();
		m_snapshots.clear();
		m_pendingEdits.clear();
	}

	EntityHandle entity = selection.GetActive();
	if (entity)
	{
		OPTICK_CATEGORY("Inspect Entity", Optick::Category::Debug);
		DrawEntityHeader(*entity.Get());

		std::vector<BaseComponent*> components = entity->GetAllComponents();
		SortComponents(components);
		for (BaseComponent* comp : components)
		{
			// Removing a component from a context menu defers it; skip what's already gone.
			if (entity->GetComponentByName(comp->GetName()) == comp)
			{
				DrawComponent(comp, *entity.Get());
			}
		}
		AddComponentPopup(*entity.Get());
	}
	else if (BaseCore* core = selection.GetCore())
	{
		OPTICK_CATEGORY("Core::OnEditorInspect", Optick::Category::GameLogic);
		ImGui::Dummy(ImVec2(0.f, 4.f));
		ImGui::Indent(8.f);
		ImGui::TextUnformatted(core->GetName().c_str());
		ImGui::Unindent(8.f);
		ImGui::Separator();
		core->OnEditorInspect();
	}
	else
	{
		ImGui::Dummy(ImVec2(0.f, 8.f));
		ImGui::Indent(8.f);
		ImGui::TextDisabled("Nothing selected");
		ImGui::Unindent(8.f);
	}
	ImGui::End();
}


void PropertiesWidget::DrawEntityHeader(Entity& entity)
{
	ImGui::Dummy(ImVec2(0.f, 4.f));
	ImGui::Indent(8.f);

	const size_t selectedCount = Selection::Get().Count();
	if (selectedCount > 1)
	{
		ImGui::TextColored(ImVec4(ACCENT_YELLOW), "%zu entities selected (showing the active one)", selectedCount);
	}

	bool active = entity.IsActiveSelf();
	if (ImGui::Checkbox("##EntityActive", &active))
	{
		EditorOps::SetActive(entity, active);
	}
	ImGui::SameLine();

	if (!m_nameEditing || m_nameEntityGUID != entity.GetGUID())
	{
		std::snprintf(m_nameBuffer, sizeof(m_nameBuffer), "%s", entity.GetName().c_str());
		m_nameEntityGUID = entity.GetGUID();
	}
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90.f);
	ImGui::InputText("##EntityName", m_nameBuffer, sizeof(m_nameBuffer));
	m_nameEditing = ImGui::IsItemActive();
	if (ImGui::IsItemDeactivatedAfterEdit())
	{
		EditorOps::Rename(entity, m_nameBuffer);
	}

	ImGui::SameLine();
	ImGui::SetNextItemWidth(80.f);
	int layer = entity.GetLayer();
	if (ImGui::BeginCombo("##Layer", ("Layer " + std::to_string(layer)).c_str()))
	{
		for (int i = 0; i < 32; ++i)
		{
			if (ImGui::Selectable(("Layer " + std::to_string(i)).c_str(), i == layer))
			{
				EditorOps::SetLayer(entity, static_cast<uint8_t>(i));
			}
		}
		ImGui::EndCombo();
	}

	const std::string guid = SceneSerializer::GUIDToString(entity.GetGUID());
	ImGui::TextDisabled("GUID %s", guid.c_str());
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Click to copy");
	}
	if (ImGui::IsItemClicked())
	{
		ImGui::SetClipboardText(guid.c_str());
	}

	const World::EntityRecord* record = entity.GetWorld()->GetRecord(entity.GetId());
	if (record && !record->PrefabAsset.empty())
	{
		ImGui::TextColored(ImVec4(ACCENT_BLUE), "Prefab: %s", record->PrefabAsset.c_str());
	}

	ImGui::Unindent(8.f);
	ImGui::Separator();
}


void PropertiesWidget::DrawComponent(BaseComponent* comp, Entity& entity)
{
	const std::string key = EditKey(entity, *comp);
	const bool pending = m_pendingEdits[key];
	json& snapshot = m_snapshots[key];
	if (!pending || snapshot.is_null())
	{
		snapshot = json();
		comp->Serialize(snapshot);
	}

	ImGui::PushID(comp);

	bool keep = true;
	const bool isTransform = comp->GetName() == "Transform";
	const bool open = ImGui::CollapsingHeader("##header", isTransform ? nullptr : &keep, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
	DrawComponentContextMenu(comp, entity);

	// Enabled toggle + name drawn over the header.
	ImGui::SameLine(ImGui::GetTreeNodeToLabelSpacing());
	bool enabled = comp->IsEnabled();
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1.f, 1.f));
	if (!isTransform && ImGui::Checkbox("##enabled", &enabled))
	{
		json before = snapshot;
		comp->SetEnabled(enabled);
		GetEngine().GetWorld().lock()->Simulate();
		json after;
		comp->Serialize(after);
		EditorOps::RecordComponentEdit(entity, comp->GetName(), before, after, enabled ? "Enable " + comp->GetName() : "Disable " + comp->GetName());
		snapshot = after;
	}
	ImGui::PopStyleVar();
	ImGui::SameLine();
	ImGui::TextUnformatted(comp->GetName().c_str());

	if (open)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.f, ImGui::GetStyle().ItemSpacing.y });
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 20.f, 0.f });
		ImGui::BeginGroup();
		ImGui::BeginDisabled(!comp->IsEnabled());
		comp->OnEditorInspect();
		ImGui::EndDisabled();
		ImGui::EndGroup();
		ImGui::PopStyleVar(2);

		const bool active = ImGui::IsItemActive();
		const bool edited = ImGui::IsItemEdited();
		const bool deactivated = ImGui::IsItemDeactivated();
		const bool clickedInside = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
		if (edited)
		{
			m_pendingEdits[key] = true;
		}

		// Commit once the interaction ends. Buttons and popups don't report edits, so a click
		// inside the component also compares state.
		if (!active && (m_pendingEdits[key] || deactivated || clickedInside))
		{
			json after;
			comp->Serialize(after);
			if (after != snapshot)
			{
				EditorOps::RecordComponentEdit(entity, comp->GetName(), snapshot, after, "Edit " + comp->GetName());
				snapshot = after;
			}
			m_pendingEdits[key] = false;
		}
		ImGui::Dummy(ImVec2(0.f, 6.f));
	}
	ImGui::PopID();

	if (!keep)
	{
		EditorOps::RemoveComponent(entity, comp->GetName());
	}
}


void PropertiesWidget::DrawComponentContextMenu(BaseComponent* comp, Entity& entity)
{
	if (!ImGui::BeginPopupContextItem("ComponentContext"))
	{
		return;
	}
	const bool isTransform = comp->GetName() == "Transform";
	if (ImGui::MenuItem("Copy Values"))
	{
		json values;
		comp->Serialize(values);
		const std::string text = std::string(kComponentClipboardPrefix) + values.dump();
		ImGui::SetClipboardText(text.c_str());
	}

	const char* clipboard = ImGui::GetClipboardText();
	json pasted;
	if (clipboard && std::strncmp(clipboard, kComponentClipboardPrefix, std::strlen(kComponentClipboardPrefix)) == 0)
	{
		pasted = json::parse(clipboard + std::strlen(kComponentClipboardPrefix), nullptr, false);
	}
	const bool canPaste = !pasted.is_discarded() && pasted.is_object() && pasted.value("Type", std::string()) == comp->GetName();
	if (ImGui::MenuItem("Paste Values", nullptr, false, canPaste))
	{
		json before;
		comp->Serialize(before);
		EditorOps::ApplyComponent(entity.GetGUID(), comp->GetName(), pasted);
		json after;
		comp->Serialize(after);
		EditorOps::RecordComponentEdit(entity, comp->GetName(), before, after, "Paste " + comp->GetName() + " Values");
	}

	ImGui::Separator();
	if (ImGui::MenuItem("Remove Component", nullptr, false, !isTransform))
	{
		EditorOps::RemoveComponent(entity, comp->GetName());
	}
	ImGui::EndPopup();
}


void PropertiesWidget::AddComponentPopup(Entity& entity)
{
	ImGui::Dummy(ImVec2(0.f, 4.f));
	ImGui::Indent(20.f);
	if (ImGui::Button("Add Component", { ImGui::GetContentRegionAvail().x - 20.f, 26.f }))
	{
		ImGui::OpenPopup("AddComponentPopup");
	}
	ImGui::Unindent(20.f);

	if (ImGui::BeginPopup("AddComponentPopup"))
	{
		CommonUtils::DrawAddComponentList(entity.GetHandle(), m_addComponentFilter, sizeof(m_addComponentFilter));
		ImGui::EndPopup();
	}
}

#endif
