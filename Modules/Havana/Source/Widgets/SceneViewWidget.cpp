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

SceneViewWidget::SceneViewWidget(const std::string& inTitle,  bool inSceneToolsEnabled)
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
}

void SceneViewWidget::Destroy()
{
	WindowFlags = 0;
	MainCamera = nullptr;
}

void SceneViewWidget::SetData(Moonlight::CameraData& data)
{
	MainCamera = &data;
}

bool SceneViewWidget::OnEvent(const BaseEvent& evt)
{
	if (evt.GetEventId() == ClearInspectEvent::GetEventId())
	{
		SelectedTransform = nullptr;
	}
	else if (evt.GetEventId() == InspectEvent::GetEventId())
	{
		const InspectEvent& event = static_cast<const InspectEvent&>(evt);

		const EntityHandle& selectedEntity = event.SelectedEntity;
		SelectedTransform = event.SelectedTransform;
		if (!SelectedTransform && selectedEntity)
		{
			if (selectedEntity->HasComponent<Transform>())
			{
				SelectedTransform = &selectedEntity->GetComponent<Transform>();
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
		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 150.f);

		if (ImGui::Button("Toggle Fullscreen", ImVec2(150.f, 20.f)))
		{
			MaximizeOnPlay = !MaximizeOnPlay;
		}

		ImGui::EndMenuBar();
	}

	ImVec2 renderPosition = ImGui::GetCursorScreenPos();

	SceneViewRenderSize = Vector2(ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);
	SceneViewRenderLocation = Vector2(renderPosition.x, renderPosition.y);
	IsFocused = ImGui::IsWindowFocused();

	Moonlight::FrameBuffer* currentView = MainCamera->Buffer;

	if (currentView && bgfx::isValid(currentView->Buffer))
	{
		ImVec2 maxPos = ImVec2(renderPosition.x + ImGui::GetWindowSize().x, renderPosition.y + ImGui::GetWindowSize().y);

		ImGui::Image(bgfx::getTexture(currentView->Buffer), ImVec2(SceneViewRenderSize.x, SceneViewRenderSize.y - 38.f), ImVec2(0, 0),
			ImVec2(Mathf::Clamp(0.f, 1.0f, SceneViewRenderSize.x / currentView->Width), Mathf::Clamp(0.f, 1.0f, SceneViewRenderSize.y / currentView->Height)));

		//if (App->Editor->GetViewportMode() == ViewportMode::Editor && gameView)
		//{
		//	ImGui::Begin("Game Preview");
		//	//ImGui::SetCursorPos(ImVec2(WorldViewRenderSize.x - 215.f, -115.f));
		//	Vector2 size(gameView->Width / 4.f, gameView->Height / 4.f);
		//	Camera::CurrentCamera->OutputSize = size;
		//	ImGui::Image(bgfx::getTexture(gameView->Buffer), ImVec2(size.x, size.y), ImVec2(0, 0),
		//		ImVec2(Mathf::Clamp(0.f, 1.0f, size.x / gameView->Width), Mathf::Clamp(0.f, 1.0f, size.y / gameView->Height)));
		//	ImGui::End();
		//}

	}

	if (EnableSceneTools)
	{
		DrawGuizmo();
	}

	ImGui::End();
	ImGui::PopStyleVar(3);
}

void SceneViewWidget::DrawGuizmo()
{
	Input& editorInput = GetEngine().GetEditorInput();
	{
		ImGui::SetCursorPos(ImVec2(5.f, 45.f));

		ImGuizmo::SetRect(SceneViewRenderLocation.x, SceneViewRenderLocation.y, SceneViewRenderSize.x, SceneViewRenderSize.y);

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
			CurrentGizmoOperation = ImGuizmo::TRANSLATE;
		ImGui::SameLine();
		if (ImGui::RadioButton("Rotate", CurrentGizmoOperation == ImGuizmo::ROTATE))
			CurrentGizmoOperation = ImGuizmo::ROTATE;
		ImGui::SameLine();
		if (ImGui::RadioButton("Scale", CurrentGizmoOperation == ImGuizmo::SCALE))
			CurrentGizmoOperation = ImGuizmo::SCALE;
		if (CurrentGizmoOperation != ImGuizmo::SCALE)
		{
			ImGui::SetCursorPosX(5.f);
			if (ImGui::RadioButton("Local", CurrentGizmoMode == ImGuizmo::LOCAL))
				CurrentGizmoMode = ImGuizmo::LOCAL;
			ImGui::SameLine();
			if (ImGui::RadioButton("World", CurrentGizmoMode == ImGuizmo::WORLD))
				CurrentGizmoMode = ImGuizmo::WORLD;
		}
	}
	if (SelectedTransform)
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

		float* matrix = &SelectedTransform->GetMatrix().GetInternalMatrix()[0].x;

		Vector3 currentPos, currentRot, currentScale;
		ImGuizmo::DecomposeMatrixToComponents(matrix, &currentPos.x, &currentRot.x, &currentScale.x);

		float viewManipulateRight = io.DisplaySize.x;
		float viewManipulateTop = 0;

		ImGuizmo::SetRect(SceneViewRenderLocation.x, SceneViewRenderLocation.y, SceneViewRenderSize.x, SceneViewRenderSize.y);
		ImGuizmo::Manipulate(&cameraView[0][0], cameraProjection, CurrentGizmoOperation, CurrentGizmoMode, matrix);
		//ImGuizmo::ViewManipulate(cameraView, 8.f, ImVec2(SceneViewRenderLocation.x + SceneViewRenderSize.x - 128, SceneViewRenderLocation.y), ImVec2(128, 128), 0x00101010);

		Vector3 modifiedPos, modifiedRot, modifiedScale;
		ImGuizmo::DecomposeMatrixToComponents(matrix, &modifiedPos.x, &modifiedRot.x, &modifiedScale.x);
		if (currentPos != modifiedPos)
		{
			SelectedTransform->SetWorldPosition(modifiedPos);
		}
		if (currentRot != modifiedRot)
		{
			SelectedTransform->SetRotation(modifiedRot);
		}
		if (currentScale != modifiedScale)
		{
			SelectedTransform->SetScale(modifiedScale);
		}
	}
}