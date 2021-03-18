#include "SceneViewWidget.h"
#include <EditorApp.h>
#include <Engine/Engine.h>
#include <ImGuizmo.h>
#include <Utils/ImGuiUtils.h>
#include <Device/FrameBuffer.h>

SceneViewWidget::SceneViewWidget(const std::string& inTitle,  bool inSceneToolsEnabled)
	: HavanaWidget(inTitle)
	, EnableSceneTools(inSceneToolsEnabled)
{
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

	ImGuizmo::SetRect(SceneViewRenderLocation.x, SceneViewRenderLocation.y, SceneViewRenderSize.x, SceneViewRenderSize.y);

	static ImGuizmo::OPERATION mCurrentGizmoOperation(ImGuizmo::TRANSLATE);
	static ImGuizmo::MODE mCurrentGizmoMode(ImGuizmo::LOCAL);


	ImGui::SetCursorPos(ImVec2(5.f, 45.f));
	if (!editorInput.IsMouseButtonDown(MouseButton::Right))
	{
		if (editorInput.IsKeyDown(KeyCode::W))
			mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
		if (editorInput.IsKeyDown(KeyCode::E))
			mCurrentGizmoOperation = ImGuizmo::ROTATE;
		if (editorInput.IsKeyDown(KeyCode::R))
			mCurrentGizmoOperation = ImGuizmo::SCALE;
	}
	if (ImGui::RadioButton("Translate", mCurrentGizmoOperation == ImGuizmo::TRANSLATE))
		mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
	ImGui::SameLine();
	if (ImGui::RadioButton("Rotate", mCurrentGizmoOperation == ImGuizmo::ROTATE))
		mCurrentGizmoOperation = ImGuizmo::ROTATE;
	ImGui::SameLine();
	if (ImGui::RadioButton("Scale", mCurrentGizmoOperation == ImGuizmo::SCALE))
		mCurrentGizmoOperation = ImGuizmo::SCALE;
	if (mCurrentGizmoOperation != ImGuizmo::SCALE)
	{
		ImGui::SetCursorPosX(5.f);
		if (ImGui::RadioButton("Local", mCurrentGizmoMode == ImGuizmo::LOCAL))
			mCurrentGizmoMode = ImGuizmo::LOCAL;
		ImGui::SameLine();
		if (ImGui::RadioButton("World", mCurrentGizmoMode == ImGuizmo::WORLD))
			mCurrentGizmoMode = ImGuizmo::WORLD;
	}
}