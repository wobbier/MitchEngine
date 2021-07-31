#include "Havana.h"
#include <imgui.h>

#include "Engine/World.h"
#include "Components/Transform.h"
#include "Components/Camera.h"
#include "ECS/Core.h"
#include "Engine/Engine.h"

#include "HavanaEvents.h"
#include "Events/SceneEvents.h"
#if ME_EDITOR
#include <filesystem>
#include "Math/Vector2.h"
#include "CLog.h"
#include "Events/EventManager.h"
#include "Math/Vector3.h"
#include "EditorApp.h"
#include "Components/Graphics/Model.h"
#include "optick.h"

#include "Profiling/BasicFrameProfile.h"

#include <Widgets/AssetBrowser.h>
#include <Widgets/LogWidget.h>
#include <Widgets/MainMenuWidget.h>
#include <Widgets/ResourceMonitorWidget.h>
#include <Widgets/SceneHierarchyWidget.h>
#include <Widgets/SceneViewWidget.h>
#include <Utils/CommonUtils.h>
#include <Widgets/PropertiesWidget.h>
#include <Widgets/AssetPreviewWidget.h>
#include <BGFXRenderer.h>

Havana::Havana(Engine* GameEngine, EditorApp* app)
	: m_engine(GameEngine)
	, m_app(app)
	, m_assetBrowser(Path("Assets").FullPath, std::chrono::milliseconds(300))
{
	m_assetBrowser.Start([](const std::string& path_to_watch, FileStatus status) -> void {
		// Process only regular files, all other file types are ignored
		if (!std::filesystem::is_regular_file(std::filesystem::path(path_to_watch)) && status != FileStatus::Deleted)
		{
			return;
		}

		switch (status) {
		case FileStatus::Created:
		{
 #if ME_PLATFORM_WIN64
			CLog::GetInstance().Log(CLog::LogType::Info, "File created: " + path_to_watch);
			TestEditorEvent evt;
			evt.Path = std::move(path_to_watch);
			evt.Queue();
#endif
		}
		break;
		case FileStatus::Modified:
			CLog::GetInstance().Log(CLog::LogType::Info, "File modified: " + path_to_watch);
			break;
		case FileStatus::Deleted:
			CLog::GetInstance().Log(CLog::LogType::Info, "File deleted: " + path_to_watch);
			break;
		default:
			CLog::GetInstance().Log(CLog::LogType::Error, "Error! Unknown file status.");
		}
		});

	std::vector<TypeId> events;
	events.push_back(TestEditorEvent::GetEventId());
	events.push_back(LoadSceneEvent::GetEventId());
	EventManager::GetInstance().RegisterReceiver(this, events);
	Renderer = &GameEngine->GetRenderer();

	MainMenu.reset(new MainMenuWidget());
	RegisteredWidgets.push_back(MainMenu);

	LogPanel.reset(new LogWidget());
	RegisteredWidgets.push_back(LogPanel);

	ResourceMonitor.reset(new ResourceMonitorWidget());
	RegisteredWidgets.push_back(ResourceMonitor);

	MainSceneView.reset(new SceneViewWidget("World View", true));
	GameSceneView.reset(new SceneViewWidget("Game View"));
	RegisteredWidgets.push_back(GameSceneView);

	SceneHierarchy.reset(new SceneHierarchyWidget());
	RegisteredWidgets.push_back(SceneHierarchy);

	PropertiesView.reset(new PropertiesWidget());
	RegisteredWidgets.push_back(PropertiesView);

	AssetPreview.reset(new AssetPreviewWidget());
	RegisteredWidgets.push_back(AssetPreview);

	InitUI();
}

void Havana::InitUI()
{
	EngineConfigFilePath = Path("Assets/Config/imgui.cfg");
	//ConfigFilePath = Path("Assets/Config/imgui.ini", true);
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	//ImGui::LoadIniSettingsFromDisk(EngineConfigFilePath.FullPath.c_str());
	io.IniFilename = EngineConfigFilePath.FullPath.c_str();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	//io.MouseDrawCursor = true;

	ImVec4* colors = ImGui::GetStyle().Colors;
	colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
	colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
	colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.07f, 0.07f, 1.0f);
	colors[ImGuiCol_ChildBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.00f);
	colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
	colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
	colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.21f, 0.22f, 0.54f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.40f, 0.40f, 0.40f, 0.40f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.18f, 0.18f, 0.67f);
	colors[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.29f, 0.29f, 0.29f, 1.00f);
	colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
	colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
	colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
	colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
	colors[ImGuiCol_CheckMark] = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
	colors[ImGuiCol_SliderGrab] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.86f, 0.86f, 0.86f, 1.00f);
	colors[ImGuiCol_Button] = ImVec4(0.44f, 0.44f, 0.44f, 0.40f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.46f, 0.47f, 0.48f, 1.00f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.42f, 0.42f, 0.42f, 1.00f);
	colors[ImGuiCol_Header] = ImVec4(0.70f, 0.70f, 0.70f, 0.31f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.70f, 0.70f, 0.70f, 0.80f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.48f, 0.50f, 0.52f, 1.00f);
	colors[ImGuiCol_Separator] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
	colors[ImGuiCol_SeparatorHovered] = ImVec4(0.72f, 0.72f, 0.72f, 0.78f);
	colors[ImGuiCol_SeparatorActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
	colors[ImGuiCol_ResizeGrip] = ImVec4(0.91f, 0.91f, 0.91f, 0.25f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.81f, 0.81f, 0.81f, 0.67f);
	colors[ImGuiCol_ResizeGripActive] = ImVec4(0.46f, 0.46f, 0.46f, 0.95f);
	colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
	colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
	colors[ImGuiCol_PlotHistogram] = ImVec4(0.73f, 0.60f, 0.15f, 1.00f);
	colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
	colors[ImGuiCol_TextSelectedBg] = ImVec4(0.87f, 0.87f, 0.87f, 0.35f);
	//colors[ImGuiCol_ModalWindowDarkening] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
	colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
	colors[ImGuiCol_NavHighlight] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
	//ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}
	style.WindowMinSize = ImVec2(10.f, 10.f);
	style.WindowBorderSize = 0.f;
	style.ScrollbarSize = 10.f;
	style.ChildBorderSize = 0.f;
	style.WindowMenuButtonPosition = ImGuiDir_None;
	style.AntiAliasedFill = false;
	style.ColorButtonPosition = ImGuiDir_Left;

	//auto cb = [this](const Vector2& pos) -> std::optional<SDL_HitTestResult>
	//{
	//	if (pos > TitleBarDragPosition && pos < TitleBarDragPosition + TitleBarDragSize)
	//	{
	//		return SDL_HitTestResult::SDL_HITTEST_DRAGGABLE;
	//	}
	//	
	//	return std::nullopt;
	//};
	//auto window = static_cast<SDLWindow*>(m_engine->GetWindow());
	/*window->SetBorderless(true);
	window->SetCustomDragCallback(cb);*/
	//m_engine->GetInput().Stop();
	//GetInput().GetMouse().SetWindow(GetActiveWindow());

	MainMenu->Init();
	LogPanel->Init();
	ResourceMonitor->Init();
	MainSceneView->Init();
	GameSceneView->Init();
	SceneHierarchy->Init();
	PropertiesView->Init();
	AssetPreview->Init();
}

void Havana::NewFrame()
{
	GetInput().Update();
	OPTICK_EVENT("Havana::NewFrame");

	ImGuizmo::BeginFrame();

	ImGuiIO& io = ImGui::GetIO();

	MainMenu->SetData(&RegisteredWidgets, m_app);
	MainMenu->Render();

	// Dockspace
	{
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImVec2 MainMenuSize;
		MainMenuSize.x = 0.f;
		MainMenuSize.y = 17.f;
		DockSize = viewport->Size;

		DockSize.y = viewport->Size.y - MainMenuSize.y - FrameProfile::kMinProfilerSize;
		DockPos = viewport->Pos;
		DockPos.y = viewport->Pos.y + MainMenuSize.y;

		ImGui::SetNextWindowPos(DockPos);
		ImGui::SetNextWindowSize(DockSize);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::SetNextWindowBgAlpha(0.0f);

		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		bool show_dockspace = true;
		ImGui::Begin("MainDockSpace", &show_dockspace, window_flags);
		ImGui::PopStyleVar(3);
		ImGuiID dockspace_id = ImGui::GetID("MyDockspace");
		ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

		ImGui::End();
	}

	LogPanel->Render();
	m_assetBrowser.Draw();
	ResourceMonitor->Render();
}

void Havana::SetGameCallbacks(std::function<void()> StartGameFunc, std::function<void()> PauseGameFunc, std::function<void()> StopGameFunc)
{
	MainMenu->SetCallbacks(StartGameFunc, PauseGameFunc, StopGameFunc);
}

void Havana::UpdateWorld(Transform* root, std::vector<Entity>& ents)
{
	SceneHierarchy->SetData(root, ents);
	SceneHierarchy->Render();

	PropertiesView->Render();
	AssetPreview->Render();
}

Input& Havana::GetInput()
{
	return m_engine->GetEditorInput();
}

void Havana::Render(Moonlight::CameraData& EditorCamera)
{
	OPTICK_EVENT("Editor Render", Optick::Category::Rendering);

	// Editor Scene View
	{
		MainSceneView->SetData(EditorCamera);
		MainSceneView->Render();
	}

	// Game View
	{
		int cameraId = Camera::CurrentCamera->GetCameraId();
		GameSceneView->SetData(*GetEngine().GetRenderer().GetCameraCache().Get(cameraId));
		GameSceneView->Render();

		Input& gameInput = GetEngine().GetInput();
		if (gameInput.IsKeyDown(KeyCode::Escape) && GameSceneView->IsFocused)
		{
			gameInput.Stop();
			ImGui::SetWindowFocus("Hierarchy");
		}
		else if (GameSceneView->IsFocused)
		{
			gameInput.Resume();
		}

		Camera::CurrentCamera->OutputSize = GameSceneView->SceneViewRenderSize;
	}

	// Frame Profiler
	{
		Vector2 size(ImGui::GetMainViewport()->Size.x, static_cast<float>(FrameProfile::kMinProfilerSize));
		auto pos = ImGui::GetMainViewport()->Pos;
		Vector2 position(pos.x, pos.y + ImGui::GetMainViewport()->Size.y - static_cast<float>(FrameProfile::kMinProfilerSize));

		FrameProfile::GetInstance().Render(position, size);
	}
}

void Havana::SetWindowTitle(const std::string& title)
{
	MainMenu->SetWindowTitle(title);
}

const bool Havana::IsGameFocused() const
{
	return GameSceneView->IsFocused;
}

const bool Havana::IsWorldViewFocused() const
{
	return MainSceneView->IsFocused;
}

const Vector2& Havana::GetGameOutputSize() const
{
	return GameSceneView->SceneViewRenderSize;
}

Vector2 Havana::GetWorldEditorRenderSize() const
{
	return MainSceneView->SceneViewRenderSize;
}

bool Havana::OnEvent(const BaseEvent& evt)
{
	if (evt.GetEventId() == TestEditorEvent::GetEventId())
	{
		const TestEditorEvent& test = static_cast<const TestEditorEvent&>(evt);
		//Logger::GetInstance().Log(Logger::LogType::Info, "We did it fam" + test.Path);
		return true;
	}
	if (evt.GetEventId() == LoadSceneEvent::GetEventId())
	{
		const LoadSceneEvent& test = static_cast<const LoadSceneEvent&>(evt);
		ClearInspectEvent evt;
		evt.Fire();
	}
	
	return false;
}

#endif
