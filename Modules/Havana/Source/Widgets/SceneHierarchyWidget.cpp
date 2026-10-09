#include "SceneHierarchyWidget.h"
#include <Engine/World.h>
#include <Engine/Engine.h>
#include <optick.h>
#include <EditorApp.h>
#include <Components/Transform.h>
#include <ECS/CoreDetail.h>
#include <Utils/CommonUtils.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <Events/HavanaEvents.h>
#include <Events/EventManager.h>
#include <Components/Graphics/Model.h>
#include <Types/AssetType.h>
#include "Types/AssetDescriptor.h"
#include "UI/Colors.h"
#include "Events/EditorEvents.h"
#include "World/SceneSerializer.h"
#include "Editor/Selection.h"
#include "Editor/EditorOperations.h"
#include "Editor/EditorActions.h"
#include "Editor/UndoStack.h"
#include <algorithm>
#include <cctype>
#include <cstdio>

#if USING( ME_EDITOR )

namespace
{
	constexpr const char* kEntityPayload = "DND_CHILD_TRANSFORM";

	// Entities being dragged (the payload itself only carries the legacy ParentDescriptor).
	std::vector<EntityHandle> s_dragged;

	std::string ToLower(std::string text)
	{
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return text;
	}

	bool IsPrefabInstance(Entity& entity)
	{
		const World::EntityRecord* record = entity.GetWorld()->GetRecord(entity.GetId());
		return record && !record->PrefabAsset.empty();
	}

	int RowId(const Entity& entity)
	{
		const uint64_t guid = entity.GetGUID();
		return static_cast<int>(guid ^ (guid >> 32));
	}
}


SceneHierarchyWidget::SceneHierarchyWidget()
	: HavanaWidget("Hierarchy")
{
	EventManager::GetInstance().RegisterReceiver(this, { PickingEvent::GetEventId() });
}


void SceneHierarchyWidget::Init()
{
	App = static_cast<EditorApp*>(GetEngine().GetGame());
}


void SceneHierarchyWidget::Destroy()
{
	App = nullptr;
	Entities = nullptr;
}


bool SceneHierarchyWidget::OnEvent(const BaseEvent& evt)
{
	if (evt.GetEventId() == PickingEvent::GetEventId())
	{
		// Selection handles the pick itself; the hierarchy reveals the picked row.
		const PickingEvent& event = static_cast<const PickingEvent&>(evt);
		m_scrollTo = GetEngine().GetWorld().lock()->FindEntityByIDValue(event.RawEntityID);
	}
	return false;
}


void SceneHierarchyWidget::SetData(Transform* inRoot, std::vector<Entity>& inEntities)
{
	RootTransform = inRoot;
	Entities = &inEntities;
}


void SceneHierarchyWidget::Update()
{
}


void SceneHierarchyWidget::BeginRename()
{
	EntityHandle active = Selection::Get().GetActive();
	if (!active)
	{
		return;
	}
	m_renaming = active;
	std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", active->GetName().c_str());
	m_focusRename = true;
	m_scrollTo = active;
}


bool SceneHierarchyWidget::PassesFilter(Entity& entity) const
{
	if (m_filter[0] == '\0')
	{
		return true;
	}
	const std::string filter = ToLower(m_filter);
	if (filter.rfind("t:", 0) == 0)
	{
		const std::string typeName = filter.substr(2);
		for (BaseComponent* component : entity.GetAllComponents())
		{
			if (ToLower(component->GetName()).rfind(typeName, 0) == 0)
			{
				return true;
			}
		}
		return false;
	}
	return ToLower(entity.GetName()).find(filter) != std::string::npos;
}


bool SceneHierarchyWidget::SubtreePassesFilter(Transform* node) const
{
	Entity* entity = node->Parent.Get();
	if (entity && PassesFilter(*entity))
	{
		return true;
	}
	for (Transform* child : node->GetChildren())
	{
		if (SubtreePassesFilter(child))
		{
			return true;
		}
	}
	return false;
}


void SceneHierarchyWidget::Render()
{
	if (!IsOpen)
	{
		return;
	}

	auto world = GetEngine().GetWorld().lock();

	OPTICK_CATEGORY("Havana::UpdateWorld", Optick::Category::GameLogic);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.f, 0.f });
	bool windowOpen = ImGui::Begin(Name.c_str(), &IsOpen, ImGuiWindowFlags_MenuBar);
	ImGui::PopStyleVar(1);
	if (!windowOpen || !world)
	{
		ImGui::End();
		return;
	}

	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("Create"))
		{
			EditorActions::Get().MenuItem("Entity.CreateEmpty", "Empty Entity");
			if (ImGui::MenuItem("Empty Entity (no Transform)"))
			{
				EditorOps::CreateEntity("Entity", nullptr, false);
			}
			ImGui::Separator();
			DrawCreateTemplates(Selection::Get().GetActiveTransform() ? Selection::Get().GetActiveTransform()->GetParentTransform() : nullptr);
			if (ImGui::BeginMenu("Core"))
			{
				DrawAddCoreList();
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
		ImGui::SetNextItemWidth(-1.f);
		ImGui::InputTextWithHint("##HierarchyFilter", "Search (t:Type)", m_filter, sizeof(m_filter));
		ImGui::EndMenuBar();
	}

	m_lastVisibleRows.swap(m_visibleRows);
	m_visibleRows.clear();

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.f, 0.f });
	if (ImGui::CollapsingHeader("Scene Root", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// Dropping on the header moves entities to the top level.
		if (ImGui::BeginDragDropTarget())
		{
			HandleAssetDrop(RootTransform);
			if (ImGui::AcceptDragDropPayload(kEntityPayload))
			{
				std::vector<Entity*> entities;
				for (EntityHandle& handle : s_dragged)
				{
					if (handle) entities.push_back(handle.Get());
				}
				EditorOps::Reparent(entities, RootTransform);
				s_dragged.clear();
			}
			ImGui::EndDragDropTarget();
		}

		OPTICK_CATEGORY("Entity List", Optick::Category::GameLogic);
		ImGui::PushStyleColor(ImGuiCol_Header, COLOR_PRIMARY);
		if (RootTransform)
		{
			// Copy: drag-and-drop below may reorder the root's children mid-draw.
			const std::vector<Transform*> children = RootTransform->GetChildren();
			for (Transform* child : children)
			{
				DrawNode(child);
			}
		}
		ImGui::PopStyleColor();
		ImGui::Dummy(ImVec2(0.f, 8.f));
	}

	DrawTransformlessEntities(*world);
	DrawCores(*world);
	ImGui::PopStyleVar(1);

	// Click on empty space clears the selection; right-click offers creation.
	if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		Selection::Get().Clear();
	}
	if (ImGui::BeginPopupContextWindow("HierarchyBackground", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
	{
		EditorActions::Get().MenuItem("Entity.CreateEmpty", "Create Empty");
		EditorActions::Get().MenuItem("Edit.Paste");
		ImGui::EndPopup();
	}

	ImGui::End();
}


void SceneHierarchyWidget::DrawNode(Transform* node)
{
	Entity* entity = node->Parent.Get();
	if (!entity)
	{
		return;
	}
	if (m_filter[0] != '\0' && !SubtreePassesFilter(node))
	{
		return;
	}

	ImGui::PushID(RowId(*entity));
	m_visibleRows.push_back(entity->GetHandle());

	const std::vector<Transform*> children = node->GetChildren();
	bool open = false;
	DrawRowContents(*entity, node, !children.empty(), open);

	if (open)
	{
		for (Transform* child : children)
		{
			DrawNode(child);
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
}


void SceneHierarchyWidget::DrawRowContents(Entity& entity, Transform* transform, bool isOpenable, bool& outOpen)
{
	EntityHandle handle = entity.GetHandle();
	const bool selected = Selection::Get().Contains(handle);

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
	if (selected) flags |= ImGuiTreeNodeFlags_Selected;
	if (!isOpenable) flags |= ImGuiTreeNodeFlags_Leaf;

	// Reveal search matches and entities selected elsewhere (scene view picking).
	if (m_filter[0] != '\0')
	{
		ImGui::SetNextItemOpen(true, ImGuiCond_Always);
	}
	else if (m_scrollTo && transform && isOpenable)
	{
		Transform* target = m_scrollTo->TryGetComponent<Transform>();
		if (target && target != transform && target->IsDescendantOf(*transform))
		{
			ImGui::SetNextItemOpen(true, ImGuiCond_Always);
		}
	}

	const bool active = entity.IsActiveInHierarchy();
	ImVec4 textColor = IsPrefabInstance(entity) ? ImVec4(ACCENT_BLUE) : ImGui::GetStyleColorVec4(ImGuiCol_Text);
	if (!active)
	{
		textColor.w *= 0.45f;
	}

	const bool renaming = m_renaming == handle;
	const char* label = entity.GetName().empty() ? "(unnamed)" : entity.GetName().c_str();
	ImGui::PushStyleColor(ImGuiCol_Text, textColor);
	outOpen = ImGui::TreeNodeEx("##node", flags, "%s", renaming ? "" : label);
	ImGui::PopStyleColor();

	const ImRect rowRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
	if (m_scrollTo == handle)
	{
		ImGui::SetScrollHereY();
		m_scrollTo = EntityHandle();
	}

	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
	{
		HandleRowClick(entity);
	}
	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !isOpenable)
	{
		BeginRename();
	}

	DrawEntityContextMenu(entity, transform);
	HandleDragDrop(entity, transform);

	// Inline rename field over the row label.
	if (renaming)
	{
		ImGui::SetCursorScreenPos(ImVec2(rowRect.Min.x + ImGui::GetTreeNodeToLabelSpacing(), rowRect.Min.y));
		ImGui::SetNextItemWidth(std::max(60.f, rowRect.GetWidth() - ImGui::GetTreeNodeToLabelSpacing() - 30.f));
		if (m_focusRename)
		{
			ImGui::SetKeyboardFocusHere();
			m_focusRename = false;
		}
		const bool commit = ImGui::InputText("##rename", m_renameBuffer, sizeof(m_renameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		if (ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			m_renaming = EntityHandle();
		}
		else if (commit || ImGui::IsItemDeactivated())
		{
			EditorOps::Rename(entity, m_renameBuffer);
			m_renaming = EntityHandle();
		}
	}

	// Active toggle at the right edge of the row.
	ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight() - 4.f);
	bool selfActive = entity.IsActiveSelf();
	if (ImGui::Checkbox("##active", &selfActive))
	{
		EditorOps::SetActive(entity, selfActive);
	}
}


void SceneHierarchyWidget::HandleRowClick(Entity& entity)
{
	EntityHandle handle = entity.GetHandle();
	const ImGuiIO& io = ImGui::GetIO();
	if (io.KeyShift && m_rangeAnchor)
	{
		// Range over the rows drawn last frame.
		auto anchor = std::find(m_lastVisibleRows.begin(), m_lastVisibleRows.end(), m_rangeAnchor);
		auto clicked = std::find(m_lastVisibleRows.begin(), m_lastVisibleRows.end(), handle);
		if (anchor != m_lastVisibleRows.end() && clicked != m_lastVisibleRows.end())
		{
			if (anchor > clicked)
			{
				std::swap(anchor, clicked);
			}
			std::vector<EntityHandle> range(anchor, clicked + 1);
			if (io.KeyCtrl)
			{
				for (EntityHandle& existing : Selection::Get().GetEntities())
				{
					if (std::find(range.begin(), range.end(), existing) == range.end())
					{
						range.insert(range.begin(), existing);
					}
				}
			}
			// The clicked entity becomes the active one.
			range.erase(std::remove(range.begin(), range.end(), handle), range.end());
			range.push_back(handle);
			Selection::Get().Set(range);
			return;
		}
	}
	if (io.KeyCtrl)
	{
		Selection::Get().Toggle(handle);
	}
	else
	{
		Selection::Get().Set(handle);
	}
	m_rangeAnchor = handle;
}


void SceneHierarchyWidget::HandleDragDrop(Entity& entity, Transform* transform)
{
	if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
	{
		EntityHandle handle = entity.GetHandle();
		s_dragged.clear();
		if (Selection::Get().Contains(handle))
		{
			for (Entity* root : Selection::Get().GetRootEntities())
			{
				s_dragged.push_back(root->GetHandle());
			}
		}
		else
		{
			s_dragged.push_back(handle);
		}
		// Legacy payload: the asset browser turns a dropped transform into a prefab.
		DragParentDescriptor.Parent = transform;
		ImGui::SetDragDropPayload(kEntityPayload, &DragParentDescriptor, sizeof(ParentDescriptor));
		if (s_dragged.size() > 1)
		{
			ImGui::Text("%zu entities", s_dragged.size());
		}
		else
		{
			ImGui::Text("%s", entity.GetName().c_str());
		}
		ImGui::EndDragDropSource();
	}

	if (!transform || !ImGui::BeginDragDropTarget())
	{
		return;
	}

	HandleAssetDrop(transform);

	const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
	const float mouseY = ImGui::GetMousePos().y;
	const float edge = rect.GetHeight() * 0.25f;
	DropZone zone = DropZone::Into;
	if (mouseY < rect.Min.y + edge) zone = DropZone::Above;
	else if (mouseY > rect.Max.y - edge) zone = DropZone::Below;

	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kEntityPayload, ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
	{
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImU32 color = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
		if (zone == DropZone::Into)
		{
			drawList->AddRect(rect.Min, rect.Max, color, 0.f, 0, 2.f);
		}
		else
		{
			const float y = zone == DropZone::Above ? rect.Min.y : rect.Max.y;
			drawList->AddLine(ImVec2(rect.Min.x, y), ImVec2(rect.Max.x, y), color, 2.f);
		}

		if (payload->IsDelivery())
		{
			std::vector<Entity*> moving;
			for (EntityHandle& handle : s_dragged)
			{
				if (handle && handle.Get()->GetId() != entity.GetId())
				{
					moving.push_back(handle.Get());
				}
			}

			if (zone == DropZone::Into)
			{
				EditorOps::Reparent(moving, transform);
			}
			else
			{
				Transform* parent = transform->GetParentTransform();
				int index = static_cast<int>(transform->GetSiblingIndex()) + (zone == DropZone::Below ? 1 : 0);
				// Entities moving from earlier slots under the same parent shift the target index down.
				for (Entity* moved : moving)
				{
					Transform* movedTransform = moved->TryGetComponent<Transform>();
					if (movedTransform && movedTransform->GetParentTransform() == parent && static_cast<int>(movedTransform->GetSiblingIndex()) < index)
					{
						--index;
					}
				}
				EditorOps::Reparent(moving, parent, std::max(index, 0));
			}
			s_dragged.clear();
		}
	}
	ImGui::EndDragDropTarget();
}


void SceneHierarchyWidget::HandleAssetDrop(Transform* parent)
{
	if (!ImGui::AcceptDragDropPayload(AssetDescriptor::kDragAndDropPayload))
	{
		return;
	}
	AssetDescriptor* descriptor = AssetDescriptor::GetDragged();
	if (!descriptor)
	{
		return;
	}

	World& world = EditorOps::GetWorld();
	if (descriptor->Type == AssetType::Model)
	{
		json data;
		data["Version"] = SceneSerializer::kVersion;
		json entity;
		entity["Name"] = descriptor->Name.substr(0, descriptor->Name.find_last_of('.'));
		entity["Components"] = json::array({ json{ { "Type", "Transform" } }, json{ { "Type", "Model" }, { "ModelPath", descriptor->FullPath.GetLocalPathString() } } });
		data["Entities"] = json::array({ entity });
		EditorOps::CreateFromData(data, parent, "Add Model");
	}
	else if (descriptor->Type == AssetType::Prefab)
	{
		// Instance once to resolve the prefab, then re-create it as an undoable operation.
		EntityHandle instance = world.CreateFromPrefab(descriptor->FullPath.FullPath, parent);
		if (instance)
		{
			json data = SceneSerializer::SerializeEntities(world, { instance.Get() });
			instance->MarkForDelete();
			world.Simulate();
			EditorOps::CreateFromData(data, parent, "Instantiate Prefab");
		}
	}
}


void SceneHierarchyWidget::DrawEntityContextMenu(Entity& entity, Transform* transform)
{
	if (!ImGui::BeginPopupContextItem("EntityContext"))
	{
		return;
	}
	EntityHandle handle = entity.GetHandle();
	if (!Selection::Get().Contains(handle))
	{
		Selection::Get().Set(handle);
	}

	if (transform && ImGui::BeginMenu("Create Child"))
	{
		if (ImGui::MenuItem("Empty Entity"))
		{
			EditorOps::CreateEntity("New Entity", transform);
		}
		DrawCreateTemplates(transform);
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Add Component"))
	{
		CommonUtils::DrawAddComponentList(handle);
		ImGui::EndMenu();
	}
	ImGui::Separator();
	EditorActions::Get().MenuItem("Edit.Rename");
	EditorActions::Get().MenuItem("Edit.Duplicate");
	EditorActions::Get().MenuItem("Edit.Copy");
	EditorActions::Get().MenuItem("Edit.Cut");
	if (transform && ImGui::MenuItem("Paste As Child", nullptr, false, EditorOps::ClipboardHasEntities()))
	{
		EditorOps::Paste(transform);
	}
	ImGui::Separator();
	EditorActions::Get().MenuItem("Edit.Delete");
	ImGui::EndPopup();
}


void SceneHierarchyWidget::DrawTransformlessEntities(World& world)
{
	std::vector<EntityHandle> utility;
	Entity* rootEntity = RootTransform ? RootTransform->Parent.Get() : nullptr;
	world.ForEachEntity([&](Entity& entity) {
		if ((!rootEntity || entity.GetId() != rootEntity->GetId()) && !entity.HasComponent<Transform>() && PassesFilter(entity))
		{
			utility.push_back(entity.GetHandle());
		}
	});
	if (utility.empty())
	{
		return;
	}

	if (ImGui::CollapsingHeader("Utility", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::PushStyleColor(ImGuiCol_Header, COLOR_PRIMARY);
		for (EntityHandle& handle : utility)
		{
			Entity* entity = handle.Get();
			ImGui::PushID(RowId(*entity));
			m_visibleRows.push_back(handle);
			bool open = false;
			DrawRowContents(*entity, nullptr, false, open);
			if (open)
			{
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		ImGui::PopStyleColor();
		ImGui::Dummy(ImVec2(0.f, 8.f));
	}
}


void SceneHierarchyWidget::DrawCores(World& world)
{
	if (!ImGui::CollapsingHeader("Entity Cores", ImGuiTreeNodeFlags_DefaultOpen))
	{
		return;
	}
	ImGui::PushStyleColor(ImGuiCol_Header, COLOR_PRIMARY);
	OPTICK_CATEGORY("Entity Cores", Optick::Category::Debug);
	for (BaseCore* core : world.GetAllCores())
	{
		ImGui::PushID(core);
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
		if (Selection::Get().GetCore() == core)
		{
			flags |= ImGuiTreeNodeFlags_Selected;
		}
		ImGui::TreeNodeEx("##core", flags, "%s", core->GetName().c_str());
		if (ImGui::IsItemClicked())
		{
			Selection::Get().SetCore(core);
		}
		ImGui::PopID();
	}
	ImGui::PopStyleColor();
}


void SceneHierarchyWidget::DrawCreateTemplates(Transform* parent)
{
	// Scene-format snippets instantiated as one undoable create.
	auto create = [parent](const char* name, json components) {
		json entity;
		entity["Name"] = name;
		entity["Components"] = std::move(components);
		json data;
		data["Version"] = SceneSerializer::kVersion;
		data["Entities"] = json::array({ entity });
		EditorOps::CreateFromData(data, parent, std::string("Create ") + name);
	};
	auto mesh = [](const char* type) {
		return json{ { "Type", "Mesh" }, { "MeshType", type }, { "Material", { { "Type", "StandardMaterial" }, { "DiffuseColor", { 0.8, 0.8, 0.8 } }, { "Roughness", 0.5 } } } };
	};
	if (ImGui::BeginMenu("3D Object"))
	{
		if (ImGui::MenuItem("Cube"))
		{
			create("Cube", json::array({ json{ { "Type", "Transform" } }, mesh("Cube") }));
		}
		if (ImGui::MenuItem("Plane"))
		{
			create("Plane", json::array({ json{ { "Type", "Transform" } }, mesh("Plane") }));
		}
		for (const char* shape : { "Sphere", "Cylinder", "Capsule" })
		{
			if (ImGui::MenuItem(shape))
			{
				create(shape, json::array({ json{ { "Type", "Transform" } }, mesh(shape) }));
			}
		}
		ImGui::EndMenu();
	}
	if (ImGui::MenuItem("Camera"))
	{
		create("Camera", json::array({ json{ { "Type", "Transform" } }, json{ { "Type", "Camera" } } }));
	}
	if (ImGui::BeginMenu("Light"))
	{
		if (ImGui::MenuItem("Directional Light"))
		{
			create("Directional Light", json::array({ json{ { "Type", "Transform" }, { "Rotation", { 50.0, -30.0, 0.0 } } }, json{ { "Type", "Light" }, { "LightType", "Directional" }, { "Intensity", 3.0 }, { "CastShadows", true } } }));
		}
		if (ImGui::MenuItem("Point Light"))
		{
			create("Point Light", json::array({ json{ { "Type", "Transform" } }, json{ { "Type", "Light" }, { "LightType", "Point" }, { "Intensity", 10.0 }, { "Range", 10.0 } } }));
		}
		if (ImGui::MenuItem("Spot Light"))
		{
			create("Spot Light", json::array({ json{ { "Type", "Transform" }, { "Rotation", { 90.0, 0.0, 0.0 } } }, json{ { "Type", "Light" }, { "LightType", "Spot" }, { "Intensity", 20.0 }, { "Range", 15.0 } } }));
		}
		ImGui::EndMenu();
	}
	if (ImGui::MenuItem("Audio Source"))
	{
		create("Audio Source", json::array({ json{ { "Type", "Transform" } }, json{ { "Type", "AudioSource" } } }));
	}
}


void SceneHierarchyWidget::DrawAddCoreList()
{
	ImGui::Text("Cores");
	ImGui::Separator();

	CoreRegistry& reg = GetCoreRegistry();

	for (auto& thing : reg)
	{
		if (ImGui::Selectable(thing.first.c_str()))
		{
			GetEngine().GetWorld().lock()->AddCoreByName(thing.first);
		}
	}
}

#endif
