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
#include "Editor/ReflectionUI.h"
#include "Editor/PrefabTools.h"
#include "Editor/UndoStack.h"
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

		// The active entity's components, in inspector order, that every selected entity has.
		std::vector<EntityHandle> selected = selection.GetEntities();
		std::vector<BaseComponent*> components = entity->GetAllComponents();
		SortComponents(components);
		for (BaseComponent* comp : components)
		{
			const std::string typeName = comp->GetName();
			// Removing a component defers it; skip what's already gone.
			if (entity->GetComponentByName(typeName) != comp)
			{
				continue;
			}
			std::vector<Instance> instances;
			instances.push_back({ entity.Get(), comp, EditKey(*entity.Get(), *comp) });
			bool shared = true;
			for (EntityHandle& other : selected)
			{
				if (other == entity)
				{
					continue;
				}
				BaseComponent* otherComp = other->GetComponentByName(typeName);
				if (!otherComp)
				{
					shared = false;
					break;
				}
				instances.push_back({ other.Get(), otherComp, EditKey(*other.Get(), *otherComp) });
			}
			if (shared)
			{
				DrawComponentGroup(typeName, instances);
			}
		}
		if (selected.size() == 1)
		{
			AddComponentPopup(*entity.Get());
		}
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
		ImGui::TextColored(ImVec4(ACCENT_YELLOW), "%zu entities selected: editing shared components", selectedCount);
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

	if (Entity* root = PrefabTools::FindInstanceRoot(entity))
	{
		// Override state is re-scanned at most a few times a second.
		const double now = ImGui::GetTime();
		if (now - m_prefabCheckTime > 0.25 || m_prefabCheckRoot != root->GetGUID())
		{
			m_prefabHasOverrides = PrefabTools::HasOverrides(*root);
			m_prefabCheckTime = now;
			m_prefabCheckRoot = root->GetGUID();
		}
		const std::string asset = PrefabTools::GetPrefabAsset(*root);
		ImGui::TextColored(ImVec4(ACCENT_BLUE), "Prefab%s: %s", m_prefabHasOverrides ? " (modified)" : "", asset.c_str());
		if (root != &entity && ImGui::SmallButton("Select Root"))
		{
			Selection::Get().Set(root->GetHandle());
		}
		if (root != &entity)
		{
			ImGui::SameLine();
		}
		ImGui::BeginDisabled(!m_prefabHasOverrides);
		if (ImGui::SmallButton("Apply All"))
		{
			PrefabTools::ApplyAll(*root);
			m_prefabCheckTime = 0.0;
		}
		ImGui::SetItemTooltip("Write this instance's changes into %s and update its other instances", asset.c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton("Revert All"))
		{
			PrefabTools::RevertAll(*root);
			m_prefabCheckTime = 0.0;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::SmallButton("Unpack"))
		{
			PrefabTools::Unpack(*root);
		}
		ImGui::SetItemTooltip("Break the link to the prefab");
	}

	ImGui::Unindent(8.f);
	ImGui::Separator();
}


void PropertiesWidget::CommitEdits(const std::string& typeName, std::vector<Instance>& instances, const std::string& undoName)
{
	std::vector<EditorOps::ComponentEdit> edits;
	for (Instance& instance : instances)
	{
		json after;
		instance.Component->Serialize(after);
		json& before = m_snapshots[instance.Key];
		if (after != before)
		{
			edits.push_back({ instance.Owner->GetGUID(), typeName, before, after });
			before = after;
		}
	}
	EditorOps::RecordComponentEdits(edits, undoName);
}


void PropertiesWidget::DrawComponentGroup(const std::string& typeName, std::vector<Instance>& instances)
{
	BaseComponent* primary = instances.front().Component;
	const std::string& groupKey = instances.front().Key;
	const bool pending = m_pendingEdits[groupKey];
	for (Instance& instance : instances)
	{
		json& snapshot = m_snapshots[instance.Key];
		if (!pending || snapshot.is_null())
		{
			snapshot = json();
			instance.Component->Serialize(snapshot);
		}
	}

	ImGui::PushID(typeName.c_str());

	bool keep = true;
	const bool isTransform = typeName == "Transform";
	const bool open = ImGui::CollapsingHeader("##header", isTransform ? nullptr : &keep, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
	DrawComponentContextMenu(typeName, instances);

	// Enabled toggle + name drawn over the header.
	ImGui::SameLine(ImGui::GetTreeNodeToLabelSpacing());
	bool enabled = primary->IsEnabled();
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1.f, 1.f));
	if (!isTransform && ImGui::Checkbox("##enabled", &enabled))
	{
		for (Instance& instance : instances)
		{
			instance.Component->SetEnabled(enabled);
		}
		GetEngine().GetWorld().lock()->Simulate();
		CommitEdits(typeName, instances, (enabled ? "Enable " : "Disable ") + typeName);
	}
	ImGui::PopStyleVar();
	ImGui::SameLine();
	ImGui::TextUnformatted(typeName.c_str());

	if (open)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 20.f, 0.f });
		ImGui::Indent(8.f);
		ImGui::BeginGroup();
		ImGui::BeginDisabled(!primary->IsEnabled());

		if (const Reflection::TypeInfo* typeInfo = primary->GetTypeInfo())
		{
			ReflectionUI::Context context;
			for (size_t i = 1; i < instances.size(); ++i)
			{
				context.Others.push_back(instances[i].Component);
			}
			std::vector<std::string> overridden;
			Entity& owner = *instances.front().Owner;
			if (instances.size() == 1 && !PrefabTools::GetPrefabAsset(owner).empty())
			{
				overridden = PrefabTools::GetOverriddenFields(owner, typeName);
				context.IsOverridden = [&overridden](const std::string& field) {
					return std::find(overridden.begin(), overridden.end(), field) != overridden.end();
				};
				context.OnPrefabAction = [&owner, &typeName](const std::string& field, bool apply) {
					if (apply)
					{
						PrefabTools::ApplyField(owner, typeName, field);
					}
					else
					{
						PrefabTools::RevertField(owner, typeName, field);
					}
				};
			}
			if (ReflectionUI::DrawType(*typeInfo, primary, context))
			{
				for (const std::string& field : context.ChangedFields)
				{
					for (Instance& instance : instances)
					{
						instance.Component->OnPropertyChanged(field);
					}
				}
			}
		}
		if (instances.size() == 1)
		{
			// Custom inspector UI (extra buttons, previews, non-reflected state).
			primary->OnEditorInspect();
		}
		else if (!primary->GetTypeInfo())
		{
			ImGui::TextDisabled("Multi-editing isn't supported for %s.", typeName.c_str());
		}

		ImGui::EndDisabled();
		ImGui::EndGroup();
		ImGui::Unindent(8.f);
		ImGui::PopStyleVar();

		const bool active = ImGui::IsItemActive();
		const bool edited = ImGui::IsItemEdited();
		const bool deactivated = ImGui::IsItemDeactivated();
		const bool clickedInside = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
		if (edited)
		{
			m_pendingEdits[groupKey] = true;
		}

		// Commit once the interaction ends. Buttons and popups don't report edits, so a click
		// inside the component also compares state.
		if (!active && (m_pendingEdits[groupKey] || deactivated || clickedInside))
		{
			CommitEdits(typeName, instances, "Edit " + typeName);
			m_pendingEdits[groupKey] = false;
		}
		ImGui::Dummy(ImVec2(0.f, 6.f));
	}
	ImGui::PopID();

	if (!keep)
	{
		UndoTransaction transaction("Remove " + typeName);
		for (Instance& instance : instances)
		{
			EditorOps::RemoveComponent(*instance.Owner, typeName);
		}
	}
}


void PropertiesWidget::DrawComponentContextMenu(const std::string& typeName, std::vector<Instance>& instances)
{
	if (!ImGui::BeginPopupContextItem("ComponentContext"))
	{
		return;
	}
	BaseComponent* primary = instances.front().Component;
	const bool isTransform = typeName == "Transform";

	Entity& owner = *instances.front().Owner;
	if (instances.size() == 1 && !PrefabTools::GetPrefabAsset(owner).empty())
	{
		const std::vector<std::string> overridden = PrefabTools::GetOverriddenFields(owner, typeName);
		if (ImGui::MenuItem("Revert Component to Prefab", nullptr, false, !overridden.empty()))
		{
			UndoTransaction transaction("Revert " + typeName);
			for (const std::string& field : overridden)
			{
				PrefabTools::RevertField(owner, typeName, field);
			}
		}
		if (ImGui::MenuItem("Apply Component to Prefab", nullptr, false, !overridden.empty()))
		{
			for (const std::string& field : overridden)
			{
				PrefabTools::ApplyField(owner, typeName, field);
			}
		}
		ImGui::Separator();
	}
	if (ImGui::MenuItem("Reset", nullptr, false, primary->GetTypeInfo() != nullptr))
	{
		const json defaults = EditorOps::GetComponentDefaults(typeName);
		for (Instance& instance : instances)
		{
			EditorOps::ApplyComponent(instance.Owner->GetGUID(), typeName, defaults);
		}
		CommitEdits(typeName, instances, "Reset " + typeName);
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Copy Values"))
	{
		json values;
		primary->Serialize(values);
		const std::string text = std::string(kComponentClipboardPrefix) + values.dump();
		ImGui::SetClipboardText(text.c_str());
	}

	const char* clipboard = ImGui::GetClipboardText();
	json pasted;
	if (clipboard && std::strncmp(clipboard, kComponentClipboardPrefix, std::strlen(kComponentClipboardPrefix)) == 0)
	{
		pasted = json::parse(clipboard + std::strlen(kComponentClipboardPrefix), nullptr, false);
	}
	const bool canPaste = !pasted.is_discarded() && pasted.is_object() && pasted.value("Type", std::string()) == typeName;
	if (ImGui::MenuItem("Paste Values", nullptr, false, canPaste))
	{
		for (Instance& instance : instances)
		{
			EditorOps::ApplyComponent(instance.Owner->GetGUID(), typeName, pasted);
		}
		CommitEdits(typeName, instances, "Paste " + typeName + " Values");
	}

	ImGui::Separator();
	if (ImGui::MenuItem("Remove Component", nullptr, false, !isTransform))
	{
		UndoTransaction transaction("Remove " + typeName);
		for (Instance& instance : instances)
		{
			EditorOps::RemoveComponent(*instance.Owner, typeName);
		}
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
