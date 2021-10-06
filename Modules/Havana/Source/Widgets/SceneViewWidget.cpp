#include "SceneViewWidget.h"
#include <EditorApp.h>
#include <Engine/Engine.h>
#include <ImGuizmo.h>
#include <Utils/ImGuiUtils.h>
#include <Device/FrameBuffer.h>
#include "HavanaEvents.h"
#include "ECS/EntityHandle.h"
#include "Components/Transform.h"
#include <Math/Matrix4.h>
#include <bgfx/bgfx.h>
#include <Camera/CameraData.h>
#include <glm/gtc/matrix_transform.hpp>
#include <Mathf.h>
#include <Cores/UI/UICore.h>

#if ME_EDITOR

SceneViewWidget::SceneViewWidget(const std::string& inTitle, bool inSceneToolsEnabled)
	: HavanaWidget(inTitle)
	, EnableSceneTools(inSceneToolsEnabled)
{
	EventManager::GetInstance().RegisterReceiver(this, { ClearInspectEvent::GetEventId(), InspectEvent::GetEventId() });
}

void SceneViewWidget::Init()
{
	App = static_cast<EditorApp*>(GetEngine().GetGame());
	WindowFlags = 0;
	WindowFlags |= ImGuiWindowFlags_MenuBar;

	{
		CurrentDisplayParams.Name = "Freeform";
		CurrentDisplayParams.Type = DisplayType::FreeForm;
		DisplayOptions.push_back(CurrentDisplayParams);
	}
	{
		DisplayParams params;
		params.Name = "Widescreen";
		params.Type = DisplayType::Ratio;
		params.Extents = { 16, 9 };
		DisplayOptions.push_back(params);
	}
	{
		DisplayParams params;
		params.Name = "Ultra-Wide";
		params.Type = DisplayType::Ratio;
		params.Extents = { 21, 9 };
		DisplayOptions.push_back(params);
	}
	{
		DisplayParams params;
		params.Name = "Pro Gamer";
		params.Type = DisplayType::Ratio;
		params.Extents = { 4, 3 };
		DisplayOptions.push_back(params);
	}
	{
		DisplayParams params;
		params.Name = "720p";
		params.Type = DisplayType::Fixed;
		params.Extents = { 1280, 720 };
		DisplayOptions.push_back(params);
	}
	{
		DisplayParams params;
		params.Name = "1080p";
		params.Type = DisplayType::Fixed;
		params.Extents = { 1920, 1080 };
		DisplayOptions.push_back(params);
	}
	/*{
		DisplayParams params;
		params.Name = "iPhone X";
		params.Type = DisplayType::Fixed;
		params.Extents = { 1125, 2436 };
		DisplayOptions.push_back(params);
	}
	{
		DisplayParams params;
		params.Name = "iPhone X Landscape";
		params.Type = DisplayType::Fixed;
		params.Extents = { 2436, 1125 };
		DisplayOptions.push_back(params);
	}*/
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

bool SceneViewWidget::OnEvent(const BaseEvent& evt)
{
	if (evt.GetEventId() == ClearInspectEvent::GetEventId())
	{
		SelectedTransform.reset();
	}
	else if (evt.GetEventId() == InspectEvent::GetEventId())
	{
		const InspectEvent& event = static_cast<const InspectEvent&>(evt);

		const EntityHandle& selectedEntity = event.SelectedEntity;
		SelectedTransform = event.SelectedTransform;
		if (!SelectedTransform.lock() && selectedEntity)
		{
			if (selectedEntity->HasComponent<Transform>())
			{
				SelectedTransform = selectedEntity->GetComponent<Transform>().shared_from_this();
			}
		}
	}
	return false;
}

void SceneViewWidget::Update()
{
}

void SceneViewWidget::Render()
{
	if (!IsOpen)
	{
		return;
	}
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

	Input& gameInput = GetEngine().GetInput();
	Input& editorInput = GetEngine().GetEditorInput();

	if (MaximizeOnPlay)
	{
		ImGuiWindowFlags fullScreenFlags = WindowFlags | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;

		ImGui::SetNextWindowPos(ImVec2(App->Editor->DockPos.x, App->Editor->DockPos.y + 20.f));
		ImGui::SetNextWindowSize(ImVec2(App->Editor->DockSize.x, App->Editor->DockSize.y - 20.f));
		ImGui::Begin("Full Screen Viewport", NULL, fullScreenFlags);
	}
	else
	{
		ImGui::Begin(Name.c_str(), &IsOpen, WindowFlags);
	}

	if (ImGui::BeginMenuBar())
	{
		if (EnableSceneTools)
		{
			if (!editorInput.IsMouseButtonDown(MouseButton::Right))
			{
				if (editorInput.IsKeyDown(KeyCode::W))
					CurrentGizmoOperation = ImGuizmo::TRANSLATE;
				if (editorInput.IsKeyDown(KeyCode::E))
					CurrentGizmoOperation = ImGuizmo::ROTATE;
				if (editorInput.IsKeyDown(KeyCode::R))
					CurrentGizmoOperation = ImGuizmo::SCALE;
			}
			if (ImGui::RadioButton("Translate", CurrentGizmoOperation == ImGuizmo::TRANSLATE))
			{
				CurrentGizmoOperation = ImGuizmo::TRANSLATE;
			}
			if (ImGui::RadioButton("Rotate", CurrentGizmoOperation == ImGuizmo::ROTATE))
			{
				CurrentGizmoOperation = ImGuizmo::ROTATE;
			}
			if (ImGui::RadioButton("Scale", CurrentGizmoOperation == ImGuizmo::SCALE))
			{
				CurrentGizmoOperation = ImGuizmo::SCALE;
			}
			if (CurrentGizmoOperation != ImGuizmo::SCALE)
			{
				float pos = ImGui::GetCursorPosX();
				ImGui::SetCursorPosX(pos + 25.f);
				if (ImGui::RadioButton("Local", CurrentGizmoMode == ImGuizmo::LOCAL))
					CurrentGizmoMode = ImGuizmo::LOCAL;
				if (ImGui::RadioButton("World", CurrentGizmoMode == ImGuizmo::WORLD))
					CurrentGizmoMode = ImGuizmo::WORLD;
			}
		}

		if (!EnableSceneTools)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
			if (ImGui::BeginMenu(CurrentDisplayParams.Name.c_str()))
			{
				for (auto& params : DisplayOptions)
				{
					std::string label = std::to_string((int)params.Extents.x);
					std::string postFix;
					if (params.Type == DisplayType::Fixed)
					{
						label += "px, ";
						postFix = "px";
					}
					else if (params.Type == DisplayType::Ratio)
					{
						label += ":";
					}
					else
					{
						label += " ";
					}
					label += std::to_string((int)params.Extents.y);
					label += postFix;
					if (ImGui::MenuItem(params.Name.c_str(), (params.Type == DisplayType::FreeForm) ? "" : label.c_str()))
					{
						CurrentDisplayParams = params;
					}
				}
				ImGui::EndMenu();
			}
			ImGui::PopStyleVar(1);
		}
		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 150.f);

		if (ImGui::Button("Toggle Fullscreen", ImVec2(150.f, 20.f)))
		{
			MaximizeOnPlay = !MaximizeOnPlay;
		}

		ImGui::EndMenuBar();
	}

	SceneViewRenderLocation = Vector2(ImGui::GetCursorPos().x, ImGui::GetCursorPos().y);
	GizmoRenderLocation = Vector2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y);

	Vector2 availableSpace = Vector2(ImGui::GetWindowSize().x, ImGui::GetWindowSize().y - 38.f);
	Vector2 viewportRenderSize = availableSpace;
	float scale = 1.f;
	switch (CurrentDisplayParams.Type)
	{
	case DisplayType::FreeForm:
	{
		SceneViewRenderSize = viewportRenderSize;
		break;
	}
	case DisplayType::Fixed:
	{
		SceneViewRenderSize = CurrentDisplayParams.Extents;
		Vector2 calculatedImageSize = viewportRenderSize;

		// rs > ri ? (wi * hs/hi, hs) : (ws, hi * ws/wi)

		float ri = SceneViewRenderSize.x / SceneViewRenderSize.y;
		float rs = availableSpace.x / availableSpace.y;

		if (rs > ri)
		{
			calculatedImageSize.x = SceneViewRenderSize.x * availableSpace.y / SceneViewRenderSize.y;
		}
		else
		{
			calculatedImageSize.y = SceneViewRenderSize.y * availableSpace.x / SceneViewRenderSize.x;
		}

		calculatedImageSize.x = Mathf::Clamp(0.f, SceneViewRenderSize.x, calculatedImageSize.x);
		calculatedImageSize.y = std::min(SceneViewRenderSize.y, calculatedImageSize.y);
		SceneViewRenderLocation.x += (viewportRenderSize.x - calculatedImageSize.x) / 2.f;
		SceneViewRenderLocation.y += (viewportRenderSize.y - calculatedImageSize.y) / 2.f;
		viewportRenderSize = calculatedImageSize;

		break;
	}
	case DisplayType::Ratio:
	{
		Vector2 ratio = CurrentDisplayParams.Extents;
		Vector2 calculatedImageSize = viewportRenderSize;

		if (ratio.x > ratio.y)
		{
			SceneViewRenderSize.y = availableSpace.x;
			SceneViewRenderSize.x = availableSpace.x * (ratio.x / ratio.y);
		}

		float ri = SceneViewRenderSize.x / SceneViewRenderSize.y;
		float rs = availableSpace.x / availableSpace.y;

		if (rs > ri)
		{
			calculatedImageSize.x = SceneViewRenderSize.x * availableSpace.y / SceneViewRenderSize.y;
		}
		else
		{
			calculatedImageSize.y = SceneViewRenderSize.y * availableSpace.x / SceneViewRenderSize.x;
		}

		SceneViewRenderLocation.x += (viewportRenderSize.x - calculatedImageSize.x) / 2.f;
		SceneViewRenderLocation.y += (viewportRenderSize.y - calculatedImageSize.y) / 2.f;
		viewportRenderSize = calculatedImageSize;
		SceneViewRenderSize = calculatedImageSize;
		break;
	}
	default:
		break;
	}

	IsFocused = ImGui::IsWindowFocused();

	Moonlight::FrameBuffer* currentView = (MainCamera) ? MainCamera->Buffer : nullptr;

	if (currentView && bgfx::isValid(currentView->Buffer))
	{
		ImGui::SetCursorPos(ImVec2(SceneViewRenderLocation.x, SceneViewRenderLocation.y));
		ImVec2 maxPos = ImVec2(SceneViewRenderLocation.x + ImGui::GetWindowSize().x, SceneViewRenderLocation.y + ImGui::GetWindowSize().y);

		ImGui::Image(bgfx::getTexture(currentView->Buffer),
			ImVec2(viewportRenderSize.x * scale, (viewportRenderSize.y * scale)),
			ImVec2(0, 0),
			ImVec2(Mathf::Clamp(0.f, 1.0f, SceneViewRenderSize.x / currentView->Width), Mathf::Clamp(0.f, 1.0f, SceneViewRenderSize.y / currentView->Height)));
	}

	if (EnableSceneTools)
	{
		DrawGuizmo();
	}

	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetDescriptor::kDragAndDropPayload))
		{
			IM_ASSERT(payload->DataSize == sizeof(AssetDescriptor));
			AssetDescriptor& payload_n = *(AssetDescriptor*)payload->Data;

			if (payload_n.Type == AssetType::Prefab)
			{
			}
		}
		ImGui::EndDragDropTarget();
	}
	ImGui::End();
	ImGui::PopStyleVar(3);
}

void SceneViewWidget::DrawGuizmo()
{
	ImGui::SetCursorPos(ImVec2(5.f, 45.f));

	ImGuizmo::SetRect(SceneViewRenderLocation.x, SceneViewRenderLocation.y, SceneViewRenderSize.x, SceneViewRenderSize.y);
	if (SelectedTransform.lock())
	{
		ImGuiIO& io = ImGui::GetIO();
		bool isPerspective = MainCamera->Projection == Moonlight::ProjectionType::Perspective;

		float cameraProjection[16];
		if (isPerspective)
		{
			bx::mtxProj(cameraProjection, MainCamera->FOV, float(MainCamera->OutputSize.x) / float(MainCamera->OutputSize.y), MainCamera->Near, MainCamera->Far, bgfx::getCaps()->homogeneousDepth);
		}
		else
		{
			bx::mtxOrtho(cameraProjection, -(MainCamera->OutputSize.x / MainCamera->OrthographicSize), (MainCamera->OutputSize.x / MainCamera->OrthographicSize), -(MainCamera->OutputSize.y / MainCamera->OrthographicSize), (MainCamera->OutputSize.y / MainCamera->OrthographicSize), MainCamera->Near, MainCamera->Far, 0.f, bgfx::getCaps()->homogeneousDepth);
		}

		ImGuizmo::SetOrthographic(!isPerspective);
		ImGuizmo::SetDrawlist();

		const glm::vec3 eye = { MainCamera->Position.x, MainCamera->Position.y, MainCamera->Position.z };
		const glm::vec3 at = { MainCamera->Position.x + MainCamera->Front.x, MainCamera->Position.y + MainCamera->Front.y, MainCamera->Position.z + MainCamera->Front.z };

		auto cameraView = glm::lookAtLH(eye, at, Vector3::Up.InternalVector);

		ImGuizmo::SetID(0);
		ImGui::SetCursorPos({ 0, 0 });

		//{
		//	float* matrix = &SelectedTransform.lock()->GetMatrix().GetInternalMatrix()[0].x;
		//
		//	Vector3 currentPos, currentRot, currentScale;
		//	ImGuizmo::DecomposeMatrixToComponents(matrix, &currentPos.x, &currentRot.x, &currentScale.x);
		//
		//	float viewManipulateRight = io.DisplaySize.x;
		//	float viewManipulateTop = 0;
		//
		//	ImGuizmo::SetRect(GizmoRenderLocation.x, GizmoRenderLocation.y, SceneViewRenderSize.x, SceneViewRenderSize.y);
		//	ImGuizmo::Manipulate(&cameraView[0][0], cameraProjection, CurrentGizmoOperation, CurrentGizmoMode, matrix);
		//	//ImGuizmo::ViewManipulate(cameraView, 8.f, ImVec2(GizmoRenderLocation.x + SceneViewRenderSize.x - 128, GizmoRenderLocation.y), ImVec2(128, 128), 0x00101010);
		//
		//	Vector3 modifiedPos, modifiedRot, modifiedScale;
		//	ImGuizmo::DecomposeMatrixToComponents(matrix, &modifiedPos.x, &modifiedRot.x, &modifiedScale.x);
		//}
		//if (currentPos != modifiedPos)
		//{
		//	SelectedTransform.lock()->SetWorldPosition(modifiedPos);
		//}
		//if (currentRot != modifiedRot)
		//{
		//	SelectedTransform.lock()->SetRotation(modifiedRot);
		//}
		//if (currentScale != modifiedScale)
		//{
		//	SelectedTransform.lock()->SetScale(modifiedScale);
		//}
	}
}

#endif