#include "SceneViewWidget.h"
#include <EditorApp.h>
#include "Cores/EditorCore.h"
#include <Engine/Engine.h>
#include <Renderer.h>
#include <Engine/World.h>
#include <ImGuizmo.h>
#include <Utils/ImGuiUtils.h>
#include <Device/FrameBuffer.h>
#include "ECS/EntityHandle.h"
#include "Components/Transform.h"
#include "Components/Camera.h"
#include "Components/Lighting/Light.h"
#include "Components/Effects/ParticleSystem.h"
#include "Cores/PhysicsCore.h"
#include "Components/Audio/AudioSource.h"
#include <Math/Matrix4.h>
#include <bgfx/bgfx.h>
#include <Camera/CameraData.h>
#include <Mathf.h>
#include "UI/Colors.h"
#include "Types/AssetDescriptor.h"
#include "Types/AssetType.h"
#include "World/SceneSerializer.h"
#include "Editor/Selection.h"
#include "Editor/EditorOperations.h"
#include "Editor/EditorActions.h"
#include "Editor/SceneTools.h"
#include "Editor/UndoStack.h"
#include <Utils/EditorConfig.h>
#include <imgui_internal.h>
#include "Profiling/FrameStats.h"
#include <algorithm>
#include <cstdio>
#include <cmath>

#if USING( ME_EDITOR )

namespace
{
	const char* kOperationNames[] = { "Move", "Rotate", "Scale", "Universal" };

	ImGuizmo::OPERATION ToImGuizmo(SceneViewWidget::GizmoOperation op)
	{
		switch (op)
		{
		case SceneViewWidget::GizmoOperation::Rotate: return ImGuizmo::ROTATE;
		case SceneViewWidget::GizmoOperation::Scale: return ImGuizmo::SCALE;
		case SceneViewWidget::GizmoOperation::Universal: return ImGuizmo::UNIVERSAL;
		default: return ImGuizmo::TRANSLATE;
		}
	}

	ImVec2 ToImVec2(const Vector2& v)
	{
		return ImVec2(v.x, v.y);
	}

	bool ToolbarToggle(const char* label, bool active, const char* tooltip)
	{
		if (active)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(COLOR_PRIMARY));
		}
		const bool pressed = ImGui::Button(label);
		if (active)
		{
			ImGui::PopStyleColor();
		}
		if (tooltip)
		{
			ImGui::SetItemTooltip("%s", tooltip);
		}
		return pressed;
	}
}


SceneViewWidget::SceneViewWidget(const std::string& inTitle, bool inSceneToolsEnabled)
	: HavanaWidget(inTitle)
	, EnableSceneTools(inSceneToolsEnabled)
{
}


void SceneViewWidget::Init()
{
	App = static_cast<EditorApp*>(GetEngine().GetGame());
	WindowFlags = ImGuiWindowFlags_MenuBar;
	DisplayOptions.clear();

	{
		CurrentDisplayParams.Name = "Freeform";
		CurrentDisplayParams.Type = DisplayType::FreeForm;
		DisplayOptions.push_back(CurrentDisplayParams);
	}
	DisplayOptions.push_back({ "Widescreen", { 16, 9 }, DisplayType::Ratio });
	DisplayOptions.push_back({ "Ultra-Wide", { 21, 9 }, DisplayType::Ratio });
	DisplayOptions.push_back({ "Super Ultra-Wide", { 32, 9 }, DisplayType::Ratio });
	DisplayOptions.push_back({ "4:3", { 4, 3 }, DisplayType::Ratio });
	DisplayOptions.push_back({ "720p", { 1280, 720 }, DisplayType::Fixed });
	DisplayOptions.push_back({ "1080p", { 1920, 1080 }, DisplayType::Fixed });

	EditorConfig& config = EditorConfig::GetInstance();
	MaximizeOnPlayPreference = config.GetPreference<bool>("GameView.MaximizeOnPlay", false);
	if (!EnableSceneTools)
	{
		return;
	}

	m_operation = static_cast<GizmoOperation>(std::clamp(config.GetPreference<int>("Gizmo.Operation", 0), 0, 3));
	m_mode = config.GetPreference<bool>("Gizmo.World", false) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
	m_pivotMode = config.GetPreference<bool>("Gizmo.Center", false) ? PivotMode::Center : PivotMode::Pivot;
	m_snapEnabled = config.GetPreference<bool>("Snap.Enabled", false);
	m_snapTranslate = config.GetPreference<float>("Snap.Translate", 0.5f);
	m_snapRotate = config.GetPreference<float>("Snap.Rotate", 15.f);
	m_snapScale = config.GetPreference<float>("Snap.Scale", 0.1f);

	// Gizmo shortcuts are scene-context actions; the camera owns W/E/Q while flying.
	EditorActions& actions = EditorActions::Get();
	auto notFlying = [this]() { return !ImGui::IsMouseDown(ImGuiMouseButton_Right); };
	auto gizmoAction = [&](const char* id, const char* name, ImGuiKey key, GizmoOperation op) {
		EditorAction action{ id, name, "Scene View", key, 0, [this, op]() { SetGizmoOperation(op); }, notFlying };
		action.Context = ActionContext::Scene;
		actions.Register(action);
	};
	gizmoAction("Gizmo.Translate", "Move Tool", ImGuiKey_W, GizmoOperation::Translate);
	gizmoAction("Gizmo.Rotate", "Rotate Tool", ImGuiKey_E, GizmoOperation::Rotate);
	gizmoAction("Gizmo.Scale", "Scale Tool", ImGuiKey_R, GizmoOperation::Scale);
	gizmoAction("Gizmo.Universal", "Universal Tool", ImGuiKey_T, GizmoOperation::Universal);

	EditorAction space{ "Gizmo.ToggleSpace", "Toggle Local/World", "Scene View", ImGuiKey_X, 0, [this]() { ToggleGizmoSpace(); }, notFlying };
	space.Context = ActionContext::Scene;
	actions.Register(space);
	EditorAction pivot{ "Gizmo.TogglePivot", "Toggle Pivot/Center", "Scene View", ImGuiKey_Z, 0, [this]() { TogglePivotMode(); }, notFlying };
	pivot.Context = ActionContext::Scene;
	actions.Register(pivot);
	actions.Register({ "Gizmo.ToggleSnap", "Toggle Snapping", "Scene View", 0, 0, [this]() { ToggleSnapping(); } });
}


void SceneViewWidget::Destroy()
{
	WindowFlags = 0;
	MainCamera = nullptr;
	DisplayOptions.clear();
}


void SceneViewWidget::SetData(Moonlight::CameraData& data)
{
	MainCamera = &data;
}


void SceneViewWidget::Update()
{
}


void SceneViewWidget::SavePreferences() const
{
	EditorConfig& config = EditorConfig::GetInstance();
	config.SetPreference("GameView.MaximizeOnPlay", MaximizeOnPlayPreference);
	if (!EnableSceneTools)
	{
		return;
	}
	config.SetPreference("Gizmo.Operation", static_cast<int>(m_operation));
	config.SetPreference("Gizmo.World", m_mode == ImGuizmo::WORLD);
	config.SetPreference("Gizmo.Center", m_pivotMode == PivotMode::Center);
	config.SetPreference("Snap.Enabled", m_snapEnabled);
	config.SetPreference("Snap.Translate", m_snapTranslate);
	config.SetPreference("Snap.Rotate", m_snapRotate);
	config.SetPreference("Snap.Scale", m_snapScale);
}


void SceneViewWidget::SetGizmoOperation(GizmoOperation op)
{
	m_operation = op;
	SavePreferences();
}


void SceneViewWidget::ToggleGizmoSpace()
{
	m_mode = m_mode == ImGuizmo::LOCAL ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
	SavePreferences();
}


void SceneViewWidget::TogglePivotMode()
{
	m_pivotMode = m_pivotMode == PivotMode::Pivot ? PivotMode::Center : PivotMode::Pivot;
	SavePreferences();
}


void SceneViewWidget::ToggleSnapping()
{
	m_snapEnabled = !m_snapEnabled;
	SavePreferences();
}


bool SceneViewWidget::ConsumeClick()
{
	const bool clicked = m_clicked;
	m_clicked = false;
	return clicked;
}


void SceneViewWidget::RequestClick(const Vector2& InViewportFraction)
{
	m_requestedClick = InViewportFraction;
	m_hasRequestedClick = true;
}


Matrix4 SceneViewWidget::GetProjection() const
{
	return MainCamera ? MainCamera->ProjectionMatrix : Matrix4();
}


void SceneViewWidget::Render()
{
	if (!IsOpen && !MaximizeOnPlay)
	{
		if (MainCamera)
		{
			MainCamera->ShouldRender = false;
		}
		IsViewportHovered = false;
		IsFocused = false;
		return;
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, COLOR_BACKGROUND_BORDER);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, COLOR_BACKGROUND_BORDER);

	bool shouldRender = false;
	if (MaximizeOnPlay && App && App->Editor)
	{
		ImGuiWindowFlags fullScreenFlags = WindowFlags | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar;
		ImGui::SetNextWindowPos(ImVec2(App->Editor->DockPos.x, App->Editor->DockPos.y));
		ImGui::SetNextWindowSize(ImVec2(App->Editor->DockSize.x, App->Editor->DockSize.y));
		shouldRender = ImGui::Begin("Full Screen Viewport", NULL, fullScreenFlags);
	}
	else
	{
		ImGuiWindowFlags flags = WindowFlags | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
		shouldRender = ImGui::Begin(Name.c_str(), &IsOpen, flags);
	}

	if (MainCamera)
	{
		MainCamera->ShouldRender = shouldRender && !ImGui::IsWindowCollapsed();
	}

	IsPlatformWindow = (bool)(ImGui::GetWindowViewport() != ImGui::GetMainViewport() && ImGui::GetWindowViewport()->Flags & ImGuiViewportFlags_IsPlatformWindow);

	if (ImGui::BeginMenuBar())
	{
		if (EnableSceneTools)
		{
			DrawSceneToolbar();
		}
		else
		{
			DrawGameToolbar();
		}
		ImGui::EndMenuBar();
	}

	SceneViewRenderLocation = Vector2(ImGui::GetCursorPos().x, ImGui::GetCursorPos().y);
	const Vector2 contentScreenPos(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y);

	const Vector2 availableSpace = Vector2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
	Vector2 viewportRenderSize = availableSpace;
	switch (CurrentDisplayParams.Type)
	{
	case DisplayType::FreeForm:
		SceneViewRenderSize = viewportRenderSize;
		break;
	case DisplayType::Fixed:
	case DisplayType::Ratio:
	{
		// Fit the target aspect inside the available space, centered.
		const float targetAspect = CurrentDisplayParams.Extents.x / std::max(CurrentDisplayParams.Extents.y, 1.f);
		Vector2 fitted = availableSpace;
		if (availableSpace.x / std::max(availableSpace.y, 1.f) > targetAspect)
		{
			fitted.x = availableSpace.y * targetAspect;
		}
		else
		{
			fitted.y = availableSpace.x / targetAspect;
		}
		if (CurrentDisplayParams.Type == DisplayType::Fixed)
		{
			fitted.x = std::min(fitted.x, CurrentDisplayParams.Extents.x);
			fitted.y = std::min(fitted.y, CurrentDisplayParams.Extents.y);
			SceneViewRenderSize = CurrentDisplayParams.Extents;
		}
		else
		{
			SceneViewRenderSize = fitted;
		}
		SceneViewRenderLocation.x += (availableSpace.x - fitted.x) / 2.f;
		SceneViewRenderLocation.y += (availableSpace.y - fitted.y) / 2.f;
		viewportRenderSize = fitted;
		break;
	}
	default:
		break;
	}
	const Vector2 imageScreenPos = contentScreenPos + (SceneViewRenderLocation - Vector2(ImGui::GetCursorPos().x, ImGui::GetCursorPos().y));
	GetEngine().GetInput().SetMouseOffset(imageScreenPos - Vector2(ImGui::GetWindowViewport()->Pos.x, ImGui::GetWindowViewport()->Pos.y));

	m_viewportScreenMin = imageScreenPos;
	m_viewportScreenSize = viewportRenderSize;
	IsViewportHovered = false;

	Moonlight::FrameBuffer* currentView = (MainCamera) ? MainCamera->Buffer : nullptr;
	if (currentView && bgfx::isValid(currentView->Buffer) && viewportRenderSize.x > 1.f && viewportRenderSize.y > 1.f)
	{
		ImGui::SetCursorPos(ImVec2(SceneViewRenderLocation.x, SceneViewRenderLocation.y));
		ImGui::Image(bgfx::getTexture(currentView->Buffer),
			ImVec2(viewportRenderSize.x, viewportRenderSize.y),
			ImVec2(0, 0),
			ImVec2(Mathf::Clamp(0.f, 1.0f, SceneViewRenderSize.x / currentView->Width), Mathf::Clamp(0.f, 1.0f, SceneViewRenderSize.y / currentView->Height)));

		IsViewportHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		ViewportMousePosition = Vector2(mouse.x - imageScreenPos.x, mouse.y - imageScreenPos.y) * (SceneViewRenderSize.x / std::max(viewportRenderSize.x, 1.f));
		if (m_hasRequestedClick)
		{
			m_hasRequestedClick = false;
			ViewportMousePosition = Vector2(m_requestedClick.x * SceneViewRenderSize.x, m_requestedClick.y * SceneViewRenderSize.y);
			m_clicked = true;
		}

		if (IsViewportHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)))
		{
			ImGui::SetWindowFocus();
		}
		HandleAssetDrop();
	}
	IsFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	if (EnableSceneTools && MainCamera)
	{
		DrawStatsOverlay();
		DrawIcons();
		DrawManipulator();
		DrawViewCube();
		HandleMouse();
	}

	ImGui::End();
	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor(2);
}


void SceneViewWidget::DrawSceneToolbar()
{
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.f, 2.f));
	for (int i = 0; i < 4; ++i)
	{
		static const char* shortcuts[] = { "Move (W)", "Rotate (E)", "Scale (R)", "Universal (T)" };
		if (ToolbarToggle(kOperationNames[i], static_cast<int>(m_operation) == i, shortcuts[i]))
		{
			SetGizmoOperation(static_cast<GizmoOperation>(i));
		}
	}
	ImGui::Separator();
	if (ImGui::Button(m_mode == ImGuizmo::LOCAL ? "Local" : "World"))
	{
		ToggleGizmoSpace();
	}
	ImGui::SetItemTooltip("Gizmo space (X)");
	if (ImGui::Button(m_pivotMode == PivotMode::Pivot ? "Pivot" : "Center"))
	{
		TogglePivotMode();
	}
	ImGui::SetItemTooltip("Gizmo position: active entity pivot or selection center (Z)");

	if (ToolbarToggle("Snap", m_snapEnabled, "Snapping (hold Ctrl to invert)"))
	{
		ToggleSnapping();
	}
	ImGui::SameLine(0.f, 0.f);
	if (ImGui::ArrowButton("##SnapSettings", ImGuiDir_Down))
	{
		ImGui::OpenPopup("SnapSettings");
	}
	if (ImGui::BeginPopup("SnapSettings"))
	{
		ImGui::SetNextItemWidth(100.f);
		bool changed = ImGui::DragFloat("Move", &m_snapTranslate, 0.05f, 0.001f, 1000.f, "%.3f");
		ImGui::SetNextItemWidth(100.f);
		changed |= ImGui::DragFloat("Rotate", &m_snapRotate, 1.f, 0.1f, 180.f, "%.1f deg");
		ImGui::SetNextItemWidth(100.f);
		changed |= ImGui::DragFloat("Scale", &m_snapScale, 0.01f, 0.001f, 10.f, "%.3f");
		if (changed)
		{
			SavePreferences();
		}
		ImGui::EndPopup();
	}

	ImGui::Separator();
	EditorConfig& config = EditorConfig::GetInstance();
	if (ImGui::BeginMenu("View"))
	{
		bool showGrid = config.GetPreference<bool>("Scene.ShowGrid", true);
		bool showGizmos = config.GetPreference<bool>("Scene.ShowGizmos", true);
		bool showSelection = config.GetPreference<bool>("Scene.ShowSelection", true);
		bool showStats = config.GetPreference<bool>("Scene.ShowStats", true);
		if (ImGui::MenuItem("Stats", nullptr, &showStats)) config.SetPreference("Scene.ShowStats", showStats);
		if (ImGui::MenuItem("Grid", nullptr, &showGrid)) config.SetPreference("Scene.ShowGrid", showGrid);
		if (ImGui::MenuItem("Component Gizmos", nullptr, &showGizmos)) config.SetPreference("Scene.ShowGizmos", showGizmos);
		if (ImGui::MenuItem("Selection Bounds", nullptr, &showSelection)) config.SetPreference("Scene.ShowSelection", showSelection);
		ImGui::Separator();
		EditorActions& actions = EditorActions::Get();
		actions.MenuItem("View.FrameSelected");
		actions.MenuItem("View.Perspective");
		actions.MenuItem("View.Top");
		actions.MenuItem("View.Front");
		actions.MenuItem("View.Right");
		ImGui::Separator();
		BGFXRenderer& renderer = GetEngine().GetRenderer();
		ImGui::MenuItem("Shadows", nullptr, &renderer.Shadows.Enabled);
		ImGui::MenuItem("Shadow Cascades", nullptr, &renderer.Shadows.DebugCascades);
		ImGui::MenuItem("Physics", nullptr, &PhysicsCore::DebugDrawEnabled);
		ImGui::EndMenu();
	}
	ImGui::PopStyleVar();
}


void SceneViewWidget::DrawGameToolbar()
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
	if (ImGui::BeginMenu(CurrentDisplayParams.Name.c_str()))
	{
		for (auto& params : DisplayOptions)
		{
			std::string label;
			if (params.Type == DisplayType::Fixed)
			{
				label = std::to_string((int)params.Extents.x) + "x" + std::to_string((int)params.Extents.y);
			}
			else if (params.Type == DisplayType::Ratio)
			{
				label = std::to_string((int)params.Extents.x) + ":" + std::to_string((int)params.Extents.y);
			}
			if (ImGui::MenuItem(params.Name.c_str(), label.c_str(), params.Name == CurrentDisplayParams.Name))
			{
				CurrentDisplayParams = params;
			}
		}
		ImGui::EndMenu();
	}
	ImGui::PopStyleVar(1);

	if (ImGui::MenuItem("Maximize On Play", nullptr, &MaximizeOnPlayPreference))
	{
		SavePreferences();
	}
}


Matrix4 SceneViewWidget::ComputeProxyMatrix()
{
	Transform* active = Selection::Get().GetActiveTransform();
	if (!active)
	{
		return Matrix4();
	}
	glm::mat4 proxy = active->GetLocalToWorldMatrix().GetInternalMatrix();
	if (m_pivotMode == PivotMode::Center)
	{
		const AABB bounds = SceneTools::ComputeSelectionBounds();
		if (bounds.IsValid())
		{
			const Vector3 center = bounds.GetCenter();
			proxy[3] = glm::vec4(center.x, center.y, center.z, 1.f);
		}
	}
	return Matrix4(proxy);
}


void SceneViewWidget::BeginDrag()
{
	m_dragging = true;
	m_proxyStart = m_proxy;
	m_dragged.clear();
	for (Entity* entity : Selection::Get().GetRootEntities())
	{
		if (Transform* transform = entity->TryGetComponent<Transform>())
		{
			m_dragged.push_back({ entity->GetGUID(), transform->GetLocalToWorldMatrix(), EditorOps::CaptureComponent(*entity, "Transform") });
		}
	}
}


void SceneViewWidget::EndDrag()
{
	m_dragging = false;
	World& world = EditorOps::GetWorld();
	UndoTransaction transaction(kOperationNames[static_cast<int>(m_operation)]);
	for (const DraggedEntity& dragged : m_dragged)
	{
		if (EntityHandle entity = world.FindEntityByGUID(dragged.GUID))
		{
			EditorOps::RecordComponentEdit(*entity.Get(), "Transform", dragged.Before, EditorOps::CaptureComponent(*entity.Get(), "Transform"), kOperationNames[static_cast<int>(m_operation)]);
		}
	}
	m_dragged.clear();
}


void SceneViewWidget::DrawManipulator()
{
	IsUsingGuizmo = false;
	if (!Selection::Get().GetActiveTransform())
	{
		if (m_dragging)
		{
			EndDrag();
		}
		return;
	}

	const bool isPerspective = MainCamera->Projection == Moonlight::ProjectionType::Perspective;
	ImGuizmo::SetOrthographic(!isPerspective);
	ImGuizmo::SetDrawlist();
	ImGuizmo::SetID(0);
	ImGuizmo::SetRect(m_viewportScreenMin.x, m_viewportScreenMin.y, m_viewportScreenSize.x, m_viewportScreenSize.y);

	if (!m_dragging)
	{
		m_proxy = ComputeProxyMatrix();
	}

	// Ctrl inverts the snapping toggle while dragging.
	const bool snapping = m_snapEnabled != ImGui::GetIO().KeyCtrl;
	float snap[3] = { m_snapTranslate, m_snapTranslate, m_snapTranslate };
	if (m_operation == GizmoOperation::Rotate)
	{
		snap[0] = snap[1] = snap[2] = m_snapRotate;
	}
	else if (m_operation == GizmoOperation::Scale)
	{
		snap[0] = snap[1] = snap[2] = m_snapScale;
	}

	const Matrix4 view = MainCamera->View;
	const Matrix4 projection = GetProjection();
	const ImGuizmo::MODE mode = m_operation == GizmoOperation::Scale ? ImGuizmo::LOCAL : m_mode;
	float* matrix = &m_proxy.GetInternalMatrix()[0][0];
	const bool changed = ImGuizmo::Manipulate(&view.GetInternalMatrix()[0][0], &projection.GetInternalMatrix()[0][0], ToImGuizmo(m_operation), mode, matrix, nullptr, snapping ? snap : nullptr);

	const bool using_ = ImGuizmo::IsUsing();
	IsUsingGuizmo = using_ || ImGuizmo::IsOver();
	if (using_ && !m_dragging)
	{
		BeginDrag();
	}
	if (changed && m_dragging)
	{
		// Every dragged root moves rigidly with the proxy: W = P * P0^-1 * W0.
		const glm::mat4 delta = m_proxy.GetInternalMatrix() * glm::inverse(m_proxyStart.GetInternalMatrix());
		World& world = EditorOps::GetWorld();
		for (const DraggedEntity& dragged : m_dragged)
		{
			EntityHandle entity = world.FindEntityByGUID(dragged.GUID);
			Transform* transform = entity ? entity->TryGetComponent<Transform>() : nullptr;
			if (!transform)
			{
				continue;
			}
			// Physics bodies notice the moved Transform and teleport with it.
			transform->SetWorldMatrix(Matrix4(delta * dragged.StartWorld.GetInternalMatrix()));
		}
	}
	if (!using_ && m_dragging)
	{
		EndDrag();
	}
}


void SceneViewWidget::DrawStatsOverlay()
{
	if (!EditorConfig::GetInstance().GetPreference<bool>("Scene.ShowStats", true))
	{
		return;
	}
	const FrameStats& stats = FrameStats::Get();
	const FrameStats::RenderStats& render = stats.GetRenderStats();
	char text[256];
	std::snprintf(text, sizeof(text), "%.0f fps  %.2f ms\nGPU %.2f ms  Draws %u\nTris %.1fk  Entities %zu",
		stats.GetFramesPerSecond(), stats.GetSmoothedFrameMilliseconds(), render.GpuMilliseconds, render.DrawCalls,
		render.Triangles / 1000.f, EditorOps::GetWorld().GetEntityCount());
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec2 position(m_viewportScreenMin.x + 8.f, m_viewportScreenMin.y + 8.f);
	const ImVec2 size = ImGui::CalcTextSize(text);
	drawList->AddRectFilled(ImVec2(position.x - 4.f, position.y - 3.f), ImVec2(position.x + size.x + 4.f, position.y + size.y + 3.f), IM_COL32(0, 0, 0, 140), 3.f);
	drawList->AddText(position, IM_COL32(230, 230, 230, 255), text);
}


void SceneViewWidget::DrawViewCube()
{
	if (!App || !App->EditorSceneManager)
	{
		return;
	}
	EditorCameraController& controller = App->EditorSceneManager->GetCameraController();
	const float size = 96.f;
	const ImVec2 position(m_viewportScreenMin.x + m_viewportScreenSize.x - size - 8.f, m_viewportScreenMin.y + 8.f);
	Matrix4 view = MainCamera->View;
	const glm::mat4 before = view.GetInternalMatrix();
	ImGuizmo::ViewManipulate(&view.GetInternalMatrix()[0][0], std::max((controller.GetPivot() - MainCamera->Position).Length(), 1.f), position, ImVec2(size, size), 0x10101010);
	if (view.GetInternalMatrix() != before)
	{
		// Recover the look direction from the new view and orbit there around the pivot.
		const glm::mat4 cameraWorld = glm::inverse(view.GetInternalMatrix());
		const glm::vec3 forward = glm::normalize(glm::vec3(cameraWorld[2]));
		const float pitch = -std::asin(std::clamp(forward.y, -1.f, 1.f)) * 180.f / 3.14159265f;
		const float yaw = std::atan2(forward.x, forward.z) * 180.f / 3.14159265f;
		controller.SetAngles(pitch, yaw);
	}
	if (ImGui::IsMouseHoveringRect(position, ImVec2(position.x + size, position.y + size)))
	{
		IsUsingGuizmo = true;
	}
}


void SceneViewWidget::DrawIcons()
{
	m_icons.clear();
	if (!EditorConfig::GetInstance().GetPreference<bool>("Scene.ShowGizmos", true))
	{
		return;
	}
	const glm::mat4 viewProjection = GetProjection().GetInternalMatrix() * MainCamera->View.GetInternalMatrix();
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const Vector2 viewportSize = SceneViewRenderSize;
	const float scale = m_viewportScreenSize.x / std::max(SceneViewRenderSize.x, 1.f);

	EditorOps::GetWorld().ForEachEntity([&](Entity& entity) {
		Transform* transform = entity.TryGetComponent<Transform>();
		if (!transform || !entity.IsActiveInHierarchy())
		{
			return;
		}
		const bool isCamera = entity.HasComponent<Camera>() && entity.TryGetComponent<Camera>() != Camera::EditorCamera;
		const bool isLight = entity.HasComponent<Light>();
		const bool isAudio = entity.HasComponent<AudioSource>();
		const bool isParticles = entity.HasComponent<ParticleSystem>();
		if (!isCamera && !isLight && !isAudio && !isParticles)
		{
			return;
		}
		Vector2 pixel;
		if (!SceneTools::WorldToScreen(transform->GetWorldPosition(), viewportSize, Matrix4(viewProjection), pixel))
		{
			return;
		}
		const ImVec2 center(m_viewportScreenMin.x + pixel.x * scale, m_viewportScreenMin.y + pixel.y * scale);
		if (center.x < m_viewportScreenMin.x || center.y < m_viewportScreenMin.y || center.x > m_viewportScreenMin.x + m_viewportScreenSize.x || center.y > m_viewportScreenMin.y + m_viewportScreenSize.y)
		{
			return;
		}
		const bool selected = Selection::Get().Contains(entity.GetHandle());
		const ImU32 color = selected ? IM_COL32(255, 160, 40, 255) : IM_COL32(230, 230, 230, 220);
		const ImU32 shadow = IM_COL32(0, 0, 0, 160);
		drawList->AddCircleFilled(center, 13.f, shadow);
		if (isCamera)
		{
			drawList->AddRectFilled(ImVec2(center.x - 8.f, center.y - 5.f), ImVec2(center.x + 3.f, center.y + 5.f), color, 2.f);
			drawList->AddTriangleFilled(ImVec2(center.x + 3.f, center.y), ImVec2(center.x + 9.f, center.y - 5.f), ImVec2(center.x + 9.f, center.y + 5.f), color);
		}
		else if (isLight)
		{
			drawList->AddCircleFilled(center, 4.5f, color);
			for (int i = 0; i < 8; ++i)
			{
				const float angle = i * 3.14159265f / 4.f;
				drawList->AddLine(ImVec2(center.x + std::cos(angle) * 6.5f, center.y + std::sin(angle) * 6.5f), ImVec2(center.x + std::cos(angle) * 9.5f, center.y + std::sin(angle) * 9.5f), color, 1.5f);
			}
		}
		else if (isParticles)
		{
			// A little burst of dots.
			drawList->AddCircleFilled(center, 2.5f, color);
			for (int i = 0; i < 6; ++i)
			{
				const float angle = i * 3.14159265f / 3.f + 0.3f;
				drawList->AddCircleFilled(ImVec2(center.x + std::cos(angle) * 7.f, center.y + std::sin(angle) * 7.f), 1.8f, color);
			}
		}
		else
		{
			drawList->AddRectFilled(ImVec2(center.x - 7.f, center.y - 3.f), ImVec2(center.x - 3.f, center.y + 3.f), color);
			drawList->AddTriangleFilled(ImVec2(center.x - 3.f, center.y - 3.f), ImVec2(center.x + 3.f, center.y - 8.f), ImVec2(center.x + 3.f, center.y + 8.f), color);
			drawList->AddTriangleFilled(ImVec2(center.x - 3.f, center.y + 3.f), ImVec2(center.x + 3.f, center.y - 8.f), ImVec2(center.x + 3.f, center.y + 8.f), color);
			drawList->AddCircle(ImVec2(center.x + 4.f, center.y), 6.f, color, 12, 1.5f);
		}
		m_icons.push_back({ Vector2(center.x, center.y), entity.GetHandle() });
	});
}


void SceneViewWidget::HandleMouse()
{
	const ImGuiIO& io = ImGui::GetIO();
	const Vector2 mouse(io.MousePos.x, io.MousePos.y);
	EditorCameraController* controller = (App && App->EditorSceneManager) ? &App->EditorSceneManager->GetCameraController() : nullptr;
	const bool navigating = controller && controller->IsNavigating();

	if (IsViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt && !IsUsingGuizmo && !navigating)
	{
		m_pressed = true;
		m_marquee = false;
		m_pressPosition = mouse;
		ImGui::SetWindowFocus();
	}
	if (!m_pressed)
	{
		return;
	}

	if (!m_marquee && (mouse - m_pressPosition).Length() > 5.f)
	{
		m_marquee = true;
	}

	const ImVec2 rectMin(std::min(mouse.x, m_pressPosition.x), std::min(mouse.y, m_pressPosition.y));
	const ImVec2 rectMax(std::max(mouse.x, m_pressPosition.x), std::max(mouse.y, m_pressPosition.y));
	if (m_marquee)
	{
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(rectMin, rectMax, IM_COL32(60, 140, 255, 40));
		drawList->AddRect(rectMin, rectMax, IM_COL32(90, 160, 255, 200));
	}

	if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left))
	{
		return;
	}
	m_pressed = false;
	const bool additive = io.KeyShift || io.KeyCtrl;

	if (!m_marquee)
	{
		// Icons take priority over mesh picking.
		for (const IconHit& icon : m_icons)
		{
			if ((icon.Position - mouse).Length() <= 13.f)
			{
				if (io.KeyCtrl)
				{
					Selection::Get().Toggle(icon.Entity);
				}
				else
				{
					Selection::Get().Set(icon.Entity);
				}
				return;
			}
		}
		// Mesh picking happens on the GPU; the result arrives a couple of frames later.
		m_clicked = true;
		return;
	}

	// Marquee: select pickable entities whose bounds center projects inside the rectangle.
	m_marquee = false;
	const glm::mat4 viewProjection = GetProjection().GetInternalMatrix() * MainCamera->View.GetInternalMatrix();
	const float scale = m_viewportScreenSize.x / std::max(SceneViewRenderSize.x, 1.f);
	std::vector<EntityHandle> picked = additive ? Selection::Get().GetEntities() : std::vector<EntityHandle>();
	EditorOps::GetWorld().ForEachEntity([&](Entity& entity) {
		Transform* transform = entity.TryGetComponent<Transform>();
		if (!transform || !entity.IsActiveInHierarchy() || transform == EditorOps::GetSceneRoot())
		{
			return;
		}
		const AABB bounds = SceneTools::ComputeWorldBounds(entity, false);
		const bool pickable = bounds.IsValid() || entity.HasComponent<Camera>() || entity.HasComponent<Light>() || entity.HasComponent<AudioSource>() || entity.HasComponent<ParticleSystem>();
		if (!pickable)
		{
			return;
		}
		Vector2 pixel;
		if (!SceneTools::WorldToScreen(bounds.IsValid() ? bounds.GetCenter() : transform->GetWorldPosition(), SceneViewRenderSize, Matrix4(viewProjection), pixel))
		{
			return;
		}
		const ImVec2 screen(m_viewportScreenMin.x + pixel.x * scale, m_viewportScreenMin.y + pixel.y * scale);
		if (screen.x >= rectMin.x && screen.x <= rectMax.x && screen.y >= rectMin.y && screen.y <= rectMax.y)
		{
			EntityHandle target = Selection::ResolvePickTarget(entity.GetHandle(), EntityHandle());
			if (target && std::find(picked.begin(), picked.end(), target) == picked.end())
			{
				picked.push_back(target);
			}
		}
	});
	Selection::Get().Set(picked);
}


void SceneViewWidget::HandleAssetDrop()
{
	if (!ImGui::BeginDragDropTarget())
	{
		return;
	}
	const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetDescriptor::kDragAndDropPayload);
	AssetDescriptor* descriptor = payload ? AssetDescriptor::GetDragged() : nullptr;
	if (descriptor && EnableSceneTools && (descriptor->Type == AssetType::Model || descriptor->Type == AssetType::Prefab))
	{
		// Land on the surface under the cursor, else on the ground plane, else in front of the camera.
		const Ray ray = SceneTools::ScreenRay(ViewportMousePosition, SceneViewRenderSize, MainCamera->View, GetProjection());
		Vector3 point = ray.GetPoint(10.f);
		SceneTools::RayHit hit;
		if (SceneTools::Raycast(ray, hit))
		{
			point = hit.Point;
		}
		else
		{
			const float distance = ray.IntersectPlane(Vector3::Up, 0.f);
			if (distance > 0.f)
			{
				point = ray.GetPoint(distance);
			}
		}

		World& world = EditorOps::GetWorld();
		json data;
		if (descriptor->Type == AssetType::Model)
		{
			json entity;
			entity["Name"] = descriptor->Name.substr(0, descriptor->Name.find_last_of('.'));
			entity["Components"] = json::array({ json{ { "Type", "Transform" } }, json{ { "Type", "Model" }, { "ModelPath", descriptor->FullPath.GetLocalPathString() } } });
			data["Version"] = SceneSerializer::kVersion;
			data["Entities"] = json::array({ entity });
		}
		else if (EntityHandle instance = world.CreateFromPrefab(descriptor->FullPath.FullPath, nullptr))
		{
			data = SceneSerializer::SerializeEntities(world, { instance.Get() });
			instance->MarkForDelete();
			world.Simulate();
		}

		if (!data.is_null())
		{
			// Place the root at the drop point.
			for (json& entity : data["Entities"])
			{
				if (entity.contains("Parent"))
				{
					continue;
				}
				for (json& component : entity["Components"])
				{
					if (component.value("Type", std::string()) == "Transform")
					{
						component["Position"] = { point.x, point.y, point.z };
					}
				}
			}
			EditorOps::CreateFromData(data, nullptr, "Place " + descriptor->Name);
		}
	}
	ImGui::EndDragDropTarget();
}

#endif
