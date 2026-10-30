#include "PropertiesWidget.h"
#include "Components/Physics/Ragdoll.h"
#include <optick.h>
#include <ECS/Component.h>
#include <ECS/Core.h>
#include <ECS/Entity.h>
#include <Engine/Engine.h>
#include <Engine/World.h>
#include "Cores/Rendering/RenderCore.h"
#include "Cores/EditorCore.h"
#include "Components/Camera.h"
#include "Components/Graphics/PostProcess.h"
#include <Utils/CommonUtils.h>
#include "Editor/EditorComponentInfoCache.h"
#include "Editor/EditorOperations.h"
#include "Editor/Selection.h"
#include "Editor/ReflectionUI.h"
#include "Editor/PrefabTools.h"
#include "Engine/ProjectSettings.h"
#include "Editor/UndoStack.h"
#include "World/SceneSerializer.h"
#include <Utils/HavanaUtils.h>
#include <ImGuiTheme.h>
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cmath>
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

	std::string ToLower(std::string text)
	{
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return text;
	}

	PropertiesWidget* s_instance = nullptr;
}


PropertiesWidget::PropertiesWidget()
	: HavanaWidget("Properties")
{
	s_instance = this;
	// The Ragdoll inspector's Build / Remove buttons (GAME-5): one undo step through EditorOps.
	Listen<RagdollBuildRequestEvent>([](const RagdollBuildRequestEvent& InEvent) {
		if (InEvent.Owner)
		{
			EditorOps::BuildRagdoll(*InEvent.Owner.Get(), InEvent.Clear);
		}
		return true;
	});
}


void PropertiesWidget::Init()
{
}


void PropertiesWidget::Destroy()
{
	m_snapshots.clear();
	m_pendingEdits.clear();
	m_lockedEntities.clear();
	if (s_instance == this)
	{
		s_instance = nullptr;
	}
}


PropertiesWidget* PropertiesWidget::Get()
{
	return s_instance;
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

	// Locked (ED-7): the entities locked on, while they live, whatever is selected now.
	EntityHandle entity = selection.GetActive();
	std::vector<EntityHandle> selected = selection.GetEntities();
	if (m_locked)
	{
		m_lockedEntities.erase(std::remove_if(m_lockedEntities.begin(), m_lockedEntities.end(), [](const EntityHandle& InEntity) { return !InEntity; }), m_lockedEntities.end());
		m_locked = !m_lockedEntities.empty();
		if (m_locked)
		{
			entity = m_lockedEntities.front();
			selected = m_lockedEntities;
		}
	}
	m_drawn.clear();
	DrawToolbar(static_cast<bool>(entity));
	const std::string filter = ToLower(m_fieldFilter);
	if (entity)
	{
		OPTICK_CATEGORY("Inspect Entity", Optick::Category::Debug);
		DrawEntityHeader(*entity.Get());

		// The active entity's components, in inspector order, that every selected entity has.
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
			// Field search: a component whose name matches shows whole; otherwise only its
			// matching fields, and it hides when none do.
			const bool nameMatches = filter.empty() || ToLower(typeName).find(filter) != std::string::npos;
			if (shared && !nameMatches && !(comp->GetTypeInfo() && ReflectionUI::AnyFieldMatches(*comp->GetTypeInfo(), comp, filter)))
			{
				shared = false;
			}
			if (shared)
			{
				DrawComponentGroup(typeName, instances, nameMatches ? std::string() : filter);
			}
		}
		if (selected.size() == 1 && filter.empty())
		{
			AddComponentPopup(*entity.Get());
		}
	}
	else if (BaseCore* core = selection.GetCore())
	{
		OPTICK_CATEGORY("Core::OnEditorInspect", Optick::Category::GameLogic);
		// The window has no padding (headers run edge to edge); free-standing content is inset.
		const float inset = ImGuiTheme::PanelInset();
		ImGuiTheme::SectionGap();
		ImGui::Indent(inset);
		ImGui::TextUnformatted(core->GetTypeIdInternal() == EditorCore::GetTypeId() ? "Scene Camera" : core->GetName().c_str());
		ImGui::Separator();
		core->OnEditorInspect();
		ImGui::Unindent(inset);
		if (core->GetTypeIdInternal() == RenderCore::GetTypeId())
		{
			DrawRenderCameraSettings(core->GetWorld());
		}
	}
	else
	{
		ImGuiTheme::SectionGap();
		ImGui::Indent(ImGuiTheme::PanelInset());
		ImGui::TextDisabled("Nothing selected");
		ImGui::Unindent(ImGuiTheme::PanelInset());
	}
	ImGui::End();
}


void PropertiesWidget::DrawRenderCameraSettings(World& world)
{
	if (!ImGui::CollapsingHeader("Camera Post-processing", ImGuiTreeNodeFlags_DefaultOpen))
	{
		return;
	}
	ImGui::PushID("RenderCameraSettings");
	const float inset = ImGuiTheme::PanelInset();
	ImGui::Indent(inset);
	EntityHandle cameraEntity = world.FindEntityByGUID(m_renderCameraGUID);
	if (!cameraEntity || !cameraEntity->HasComponent<Camera>())
	{
		m_renderCameraGUID = 0;
		cameraEntity.Reset();
	}
	if (m_renderCameraGUID == 0 && Camera::CurrentCamera && Camera::CurrentCamera->Parent.GetWorld() == &world)
	{
		cameraEntity = Camera::CurrentCamera->Parent;
	}
	const std::string preview = m_renderCameraGUID == 0 ? "Main Camera" : cameraEntity->GetName();
	HavanaUtils::Label("Camera");
	if (ImGui::BeginCombo("##Camera", preview.c_str()))
	{
		if (ImGui::Selectable("Main Camera", m_renderCameraGUID == 0))
		{
			m_renderCameraGUID = 0;
			cameraEntity = Camera::CurrentCamera && Camera::CurrentCamera->Parent.GetWorld() == &world ? Camera::CurrentCamera->Parent : EntityHandle();
		}
		world.Each<Camera>([&](Entity& entity, Camera&) {
			ImGui::PushID(std::to_string(entity.GetGUID()).c_str());
			if (ImGui::Selectable(entity.GetName().c_str(), entity.GetGUID() == m_renderCameraGUID))
			{
				m_renderCameraGUID = entity.GetGUID();
				cameraEntity = world.GetEntity(entity.GetId());
			}
			ImGui::PopID();
		});
		ImGui::EndCombo();
	}
	ImGui::TextWrapped("Camera edits support undo and save with the scene. The Scene View follows Main Camera.");
	if (cameraEntity)
	{
		Camera& camera = cameraEntity->GetComponent<Camera>();
		ImGui::Text("%s | %.0f x %.0f", cameraEntity->GetName().c_str(), camera.OutputSize.x, camera.OutputSize.y);
		if (!camera.IsEnabled() || !cameraEntity->IsActiveInHierarchy())
		{
			ImGui::TextDisabled("This camera is inactive.");
		}
		PostProcess* post = cameraEntity->TryGetComponent<PostProcess>();
		if (!post)
		{
			ImGui::TextWrapped("Using defaults: FXAA on, TAA off, ACES tonemapping, bloom and ambient occlusion on.");
			if (ImGui::Button("Add PostProcess"))
			{
				post = static_cast<PostProcess*>(EditorOps::AddComponent(*cameraEntity.Get(), "PostProcess"));
			}
		}
		if (post)
		{
			const char* aa = !post->IsEnabled() ? "FXAA (defaults)" : post->TemporalAA ? (post->FXAA ? "TAA + FXAA" : "TAA") : (post->FXAA ? "FXAA" : "Off");
			ImGui::Text("Scene AA: %s", aa);
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("FXAA smooths the final image. TAA combines jittered frames using depth reprojection; moving objects may ghost. Both can be enabled together. SMAA is not implemented.");
			}
			// The component group draws its own edge-to-edge header.
			ImGui::Unindent(inset);
			std::vector<Instance> instances = { { cameraEntity.Get(), post, EditKey(*cameraEntity.Get(), *post) } };
			DrawComponentGroup("PostProcess", instances, std::string());
			ImGui::Indent(inset);
		}
	}
	else
	{
		ImGui::TextWrapped("No scene camera is selected. Add a Camera to the scene to customize post-processing.");
	}
	ImGui::Unindent(inset);
	ImGui::PopID();
}


void PropertiesWidget::DrawToolbar(bool InHasEntity)
{
	// Field search, then the lock, above the entity (inset like the header).
	const float inset = ImGuiTheme::PanelInset();
	ImGuiTheme::SectionGap();
	ImGui::Indent(inset);
	const float lockWidth = ImGuiTheme::Em(4.5f);
	const float searchWidth = HavanaUtils::RowRight() - ImGui::GetCursorScreenPos().x - lockWidth - ImGui::GetStyle().ItemSpacing.x;
	ImGui::SetNextItemWidth(std::max(searchWidth, ImGuiTheme::Em(3.f)));
	ImGui::InputTextWithHint("##FieldSearch", "Search components and fields", m_fieldFilter, sizeof(m_fieldFilter));
	ImGui::SameLine();
	ImGui::BeginDisabled(!m_locked && !InHasEntity);
	if (m_locked)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
	}
	if (ImGui::Button(m_locked ? "Locked" : "Lock", ImVec2(lockWidth, 0.f)))
	{
		SetLocked(!m_locked);
	}
	if (m_locked)
	{
		ImGui::PopStyleColor();
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip(m_locked ? "Unlock: follow the selection again" : "Keep inspecting this selection while you select others");
	ImGui::Unindent(inset);
}


void PropertiesWidget::SetLocked(bool InLocked)
{
	m_locked = false;
	m_lockedEntities.clear();
	Selection& selection = Selection::Get();
	EntityHandle active = selection.GetActive();
	if (!InLocked || !active)
	{
		return;
	}
	m_lockedEntities.push_back(active);
	for (EntityHandle& other : selection.GetEntities())
	{
		if (other && other != active)
		{
			m_lockedEntities.push_back(other);
		}
	}
	m_locked = true;
}


bool PropertiesWidget::ExecuteAutomation(const std::string& InCommand, const std::string& InArgs, std::string& OutError)
{
	auto split = [](const std::string& InText, std::string& OutLeft, std::string& OutRight) {
		const size_t bar = InText.find('|');
		auto trim = [](std::string InPart) {
			InPart.erase(0, InPart.find_first_not_of(" \t"));
			InPart.erase(InPart.find_last_not_of(" \t") + 1);
			return InPart;
		};
		OutLeft = trim(InText.substr(0, bar));
		OutRight = bar == std::string::npos ? std::string() : trim(InText.substr(bar + 1));
	};
	if (InCommand == "inspector-lock")
	{
		SetLocked(InArgs == "1" || InArgs == "true");
		if ((InArgs == "1" || InArgs == "true") && !m_locked)
		{
			OutError = "nothing selected to lock on";
			return false;
		}
		return true;
	}
	if (InCommand == "inspector-search")
	{
		std::snprintf(m_fieldFilter, sizeof(m_fieldFilter), "%s", InArgs.c_str());
		return true;
	}
	if (InCommand == "inspector-assert-shows")
	{
		std::string item;
		std::string expected;
		split(InArgs, item, expected);
		const bool shown = std::find(m_drawn.begin(), m_drawn.end(), item) != m_drawn.end();
		if (shown != (expected != "0" && expected != "false"))
		{
			OutError = item + (shown ? " is shown" : " isn't shown") + " (shown: " + std::to_string(m_drawn.size()) + " items)";
			return false;
		}
		return true;
	}
	if (InCommand == "inspector-assert-entity")
	{
		EntityHandle inspected = m_locked && !m_lockedEntities.empty() ? m_lockedEntities.front() : Selection::Get().GetActive();
		const std::string name = inspected ? inspected->GetName() : std::string();
		if (name != InArgs)
		{
			OutError = "inspecting '" + name + "', not '" + InArgs + "'";
			return false;
		}
		return true;
	}
	OutError = "unknown command";
	return false;
}


void PropertiesWidget::DrawEntityHeader(Entity& entity)
{
	// The window has no padding (headers run edge to edge); the entity header is inset.
	const float inset = ImGuiTheme::PanelInset();
	ImGuiTheme::SectionGap();
	ImGui::Indent(inset);

	const size_t selectedCount = Selection::Get().Count();
	if (selectedCount > 1)
	{
		ImGui::TextColored(ImGuiTheme::WarningText(), "%zu entities selected: editing shared components", selectedCount);
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
	// Name fills the row up to the layer picker, which ends where property values end.
	const float layerWidth = ImGuiTheme::Em(5.f);
	const float nameWidth = HavanaUtils::RowRight() - ImGui::GetCursorScreenPos().x - layerWidth - ImGui::GetStyle().ItemSpacing.x;
	ImGui::SetNextItemWidth(std::max(nameWidth, ImGuiTheme::Em(3.f)));
	ImGui::InputText("##EntityName", m_nameBuffer, sizeof(m_nameBuffer));
	m_nameEditing = ImGui::IsItemActive();
	if (ImGui::IsItemDeactivatedAfterEdit())
	{
		EditorOps::Rename(entity, m_nameBuffer);
	}

	ImGui::SameLine();
	ImGui::SetNextItemWidth(layerWidth);
	int layer = entity.GetLayer();
	const ProjectSettings& settings = ProjectSettings::Get();
	if (ImGui::BeginCombo("##Layer", settings.GetLayerLabel(layer).c_str()))
	{
		for (int i = 0; i < ProjectSettings::kLayerCount; ++i)
		{
			if (ImGui::Selectable(settings.GetLayerLabel(i).c_str(), i == layer))
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
		ImGui::TextColored(ImGuiTheme::AccentText(), "Prefab%s: %s", m_prefabHasOverrides ? " (modified)" : "", asset.c_str());
		if (root != &entity && ImGui::Button("Select Root"))
		{
			Selection::Get().Set(root->GetHandle());
		}
		if (root != &entity)
		{
			ImGui::SameLine();
		}
		ImGui::BeginDisabled(!m_prefabHasOverrides);
		if (ImGui::Button("Apply All"))
		{
			PrefabTools::ApplyAll(*root);
			m_prefabCheckTime = 0.0;
		}
		ImGui::SetItemTooltip("Write this instance's changes into %s and update its other instances", asset.c_str());
		ImGui::SameLine();
		if (ImGui::Button("Revert All"))
		{
			PrefabTools::RevertAll(*root);
			m_prefabCheckTime = 0.0;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Unpack"))
		{
			PrefabTools::Unpack(*root);
		}
		ImGui::SetItemTooltip("Break the link to the prefab");
	}

	ImGui::Unindent(inset);
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


void PropertiesWidget::DrawComponentGroup(const std::string& typeName, std::vector<Instance>& instances, const std::string& InFilter)
{
	m_drawn.push_back(typeName);
	BaseComponent* primary = instances.front().Component;
	const std::string& groupKey = instances.front().Key;
	const bool pending = m_pendingEdits[groupKey];
	// Refresh the undo baseline before input can change a control, including custom inspectors
	// and their popups. Idle inspection must not serialize an entire model every frame.
	bool input = false;
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
		|| ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
	{
		const ImGuiIO& io = ImGui::GetIO();
		input = !io.InputQueueCharacters.empty() || io.MouseWheel != 0.f || io.MouseWheelH != 0.f;
		for (int button = 0; button < ImGuiMouseButton_COUNT && !input; ++button)
		{
			input = ImGui::IsMouseClicked(button) || ImGui::IsMouseReleased(button)
				|| (ImGui::IsMouseDown(button) && (io.MouseDelta.x != 0.f || io.MouseDelta.y != 0.f));
		}
		for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END && !input; ++key)
		{
			input = ImGui::IsKeyPressed(static_cast<ImGuiKey>(key));
		}
	}
	for (Instance& instance : instances)
	{
		json& snapshot = m_snapshots[instance.Key];
		if (snapshot.is_null() || (!pending && input))
		{
			snapshot = json();
			instance.Component->Serialize(snapshot);
		}
	}

	ImGui::PushID(typeName.c_str());

	bool keep = true;
	const bool isTransform = typeName == "Transform";
	const float headerLabelX = ImGui::GetCursorPosX() + ImGui::GetTreeNodeToLabelSpacing();
	const bool open = ImGui::CollapsingHeader("##header", isTransform ? nullptr : &keep, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
	DrawComponentContextMenu(typeName, instances);

	// Enabled toggle + name drawn over the header.
	ImGui::SameLine(headerLabelX);
	bool enabled = primary->IsEnabled();
	if (!isTransform)
	{
		// A compact checkbox, centered in the header row.
		const float framePadding = ImGui::GetStyle().FramePadding.y;
		const float compact = std::max(1.f, std::round(framePadding * 0.25f));
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + framePadding - compact);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(compact, compact));
		if (ImGui::Checkbox("##enabled", &enabled))
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
	}
	ImGui::TextUnformatted(typeName.c_str());

	if (open)
	{
		// Fields line up under the component's name; values end a panel inset from the edge.
		const float bodyIndent = headerLabelX - ImGui::GetCursorPosX();
		ImGui::Indent(bodyIndent);
		ImGui::BeginGroup();
		ImGui::BeginDisabled(!primary->IsEnabled());

		if (const Reflection::TypeInfo* typeInfo = primary->GetTypeInfo())
		{
			ReflectionUI::Context context;
			context.Filter = InFilter;
			std::vector<std::string> drawnFields;
			context.Drawn = &drawnFields;
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
			for (const std::string& field : drawnFields)
			{
				m_drawn.push_back(typeName + "." + field);
			}
		}
		if (instances.size() == 1 && InFilter.empty())
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
		ImGui::Unindent(bodyIndent);

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
		ImGuiTheme::SectionGap();
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
	// A full-width action at the end of the inspector, inset like the rows above it.
	ImGuiTheme::SectionGap();
	const float inset = ImGuiTheme::PanelInset();
	ImGui::Indent(inset);
	if (ImGui::Button("Add Component", ImVec2(HavanaUtils::RowRight() - ImGui::GetCursorScreenPos().x, 0.f)))
	{
		ImGui::OpenPopup("AddComponentPopup");
	}
	ImGui::Unindent(inset);

	if (ImGui::BeginPopup("AddComponentPopup"))
	{
		CommonUtils::DrawAddComponentList(entity.GetHandle(), m_addComponentFilter, sizeof(m_addComponentFilter));
		ImGui::EndPopup();
	}
}

#endif
