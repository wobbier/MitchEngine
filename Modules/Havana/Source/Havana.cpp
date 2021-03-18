#include "Havana.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
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
#include "Mathf.h"
#include "CLog.h"
#include "Resource/ResourceCache.h"
#include "Graphics/Texture.h"
#include "Widgets/AssetBrowser.h"
#include <chrono>
#include "Events/EventManager.h"
#include "Engine/Input.h"
#include "ImGuizmo.h"
#include "Math/Vector3.h"
#include "EditorApp.h"
#include "Components/Graphics/Model.h"
#include "Commands/EditorCommands.h"
#include "optick.h"
#if ME_PLATFORM_WIN64
#include <winuser.h>
#endif
#include "Utils/StringUtils.h"
#include "Profiling/BasicFrameProfile.h"
#include "Ultralight/Matrix.h"
#include "Math/Matrix4.h"
#include "Cores/EditorCore.h"
#include "Window/SDLWindow.h"
#include "SDL_video.h"
#include <optional>
#include "imgui_internal.h"
#include "Window/EditorWindow.h"
#include "Utils/ImGuiUtils.h"
#include "BGFXRenderer.h"

#include <Widgets/LogWidget.h>
#include <Widgets/MainMenuWidget.h>
#include <Widgets/ResourceMonitorWidget.h>
#include <Widgets/SceneHierarchyWidget.h>
#include <Widgets/SceneViewWidget.h>
#include <Utils/CommonUtils.h>

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
	events.push_back(PreviewResourceEvent::GetEventId());
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
}

bool show_dockspace = true;
void Havana::NewFrame()
{
	GetInput().Update();
	OPTICK_EVENT("Havana::NewFrame");

	ImGuiIO& io = ImGui::GetIO();

	// Output size fix?
	RenderSize = Vector2(io.DisplaySize.x, io.DisplaySize.y);
	//Renderer->GetDevice().SetOutputSize(RenderSize);

	MainMenu->SetData(&RegisteredWidgets, m_app);
	MainMenu->Render();

	static float f = 0.0f;
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
	ImGui::Begin("MainDockSpace", &show_dockspace, window_flags);
	ImGui::PopStyleVar(3);
	ImGuiID dockspace_id = ImGui::GetID("MyDockspace");
	ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;
	ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

	ImGui::End();

	LogPanel->Render();
	m_assetBrowser.Draw();
	ResourceMonitor->Render();

	//DrawCommandPanel();
}

void Havana::AddComponentPopup(EntityHandle inSelectedEntity)
{
	ImGui::PushItemWidth(-100);
	if (ImGui::Button("Add Component.."))
	{
		ImGui::OpenPopup("my_select_popup");
	}
	ImGui::PopItemWidth();

	if (ImGui::BeginPopup("my_select_popup"))
	{
		CommonUtils::DrawAddComponentList(inSelectedEntity);

		ImGui::EndPopup();
	}
}

void Havana::ClearSelection()
{
	SelectedTransform = nullptr;
	SelectedEntity = EntityHandle();
	SelectedCore = nullptr;
}

void Havana::DrawCommandPanel()
{
	OPTICK_CATEGORY("Command History", Optick::Category::Debug);
	/*auto& kb = GetInput().GetKeyboardState();
	tracker.Update(kb);

	if (kb.LeftControl && tracker.pressed.Z)
	{
		if (kb.LeftShift)
		{
			EditorCommands.Redo();
		}
		else
		{
			BRUH("UNDID THA DAMN TING");
			EditorCommands.Undo();
		}
	}

	EditorCommands.Draw();*/
}

void Havana::SetGameCallbacks(std::function<void()> StartGameFunc, std::function<void()> PauseGameFunc, std::function<void()> StopGameFunc)
{
	MainMenu->SetCallbacks(StartGameFunc, PauseGameFunc, StopGameFunc);
}

void Havana::UpdateWorld(Transform* root, std::vector<Entity>& ents)
{
	SceneHierarchy->SetData(root, ents);
	SceneHierarchy->Render();

	ImGui::Begin("Properties");

	EntityHandle entity = SelectedEntity;
	if (SelectedTransform != nullptr)
	{
		entity = SelectedTransform->Parent;
	}

	if (entity)
	{
		OPTICK_CATEGORY("Inspect Entity", Optick::Category::Debug);
		entity->OnEditorInspect();
		for (BaseComponent* comp : entity->GetAllComponents())
		{
			bool shouldClose = true;
			if (ImGui::CollapsingHeader(comp->GetName().c_str(), &shouldClose, ImGuiTreeNodeFlags_DefaultOpen))
			{
				comp->OnEditorInspect();
			}
			if (!shouldClose)
			{
				entity->RemoveComponent(comp->GetName());
				GetEngine().GetWorld().lock()->Simulate();
			}
		}
		AddComponentPopup(SelectedEntity);
	}

	if (SelectedCore != nullptr)
	{
		OPTICK_CATEGORY("Core::OnEditorInspect", Optick::Category::GameLogic);
		SelectedCore->OnEditorInspect();
	}
	ImGui::End();

	ImGui::Begin("Preview");
	{
		if (ViewTexture)
		{
			//if (ViewTexture->TexHandle != bgfx::kInvalidHandle)
			{
				OPTICK_CATEGORY("Preview Texture", Optick::Category::Debug);
				// Get the current cursor position (where your window is)
				ImVec2 pos = ImGui::GetCursorScreenPos();
				ImVec2 maxPos = ImVec2(pos.x + ImGui::GetWindowSize().x, pos.y + ImGui::GetWindowSize().y);
				Vector2 RenderSize = Vector2(ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);

				// Ask ImGui to draw it as an image:
				// Under OpenGL the ImGUI image type is GLuint
				// So make sure to use "(void *)tex" but not "&tex"
				/*ImGui::GetWindowDrawList()->AddImage(
					(void*)ViewTexture->TexHandle,
					ImVec2(pos.x, pos.y),
					ImVec2(maxPos));*/
					//ImVec2(WorldViewRenderSize.X() / RenderSize.X(), WorldViewRenderSize.Y() / RenderSize.Y()));

			}
		}
	}
	ImGui::End();
}

void Havana::HandleAssetDragAndDrop(Transform* root)
{
	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_ASSET_BROWSER"))
	{
		IM_ASSERT(payload->DataSize == sizeof(AssetBrowser::AssetDescriptor));
		AssetBrowser::AssetDescriptor payload_n = *(AssetBrowser::AssetDescriptor*)payload->Data;

		if (payload_n.Type == AssetBrowser::AssetType::Model)
		{
			auto ent = GetEngine().GetWorld().lock()->CreateEntity();
			ent->AddComponent<Transform>(payload_n.Name.substr(0, payload_n.Name.find_last_of('.'))).SetParent(*root);
			ent->AddComponent<Model>((payload_n.FullPath.FullPath));
		}
		if (payload_n.Type == AssetBrowser::AssetType::Prefab)
		{
			EntityHandle ent = GetEngine().GetWorld().lock()->CreateFromPrefab(payload_n.FullPath.FullPath, root);
		}
	}
}

Input& Havana::GetInput()
{
	return m_engine->GetEditorInput();
}

void Havana::Render(Moonlight::CameraData& EditorCamera)
{
	OPTICK_EVENT("Editor Render", Optick::Category::Rendering);
	auto& io = ImGui::GetIO();

	MainSceneView->SetData(EditorCamera);
	MainSceneView->Render();

	int cameraId = Camera::CurrentCamera->GetCameraId();
	GameSceneView->SetData(GetEngine().GetRenderer().GetCamera(cameraId));
	GameSceneView->Render();

	Camera::CurrentCamera->OutputSize = GameSceneView->SceneViewRenderSize;

	Vector2 size(ImGui::GetMainViewport()->Size.x, static_cast<float>(FrameProfile::kMinProfilerSize));
	auto pos = ImGui::GetMainViewport()->Pos;
	Vector2 position(pos.x, pos.y + ImGui::GetMainViewport()->Size.y - static_cast<float>(FrameProfile::kMinProfilerSize));

	FrameProfile::GetInstance().Render(position, size);

	//ImGui::Render();
	//ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	// Update and Render additional Platform Windows
}

void Havana::SetViewportMode(ViewportMode mode)
{
	m_viewportMode = mode;
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
	return GameRenderSize;
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
		ClearSelection();
	}
	if (evt.GetEventId() == PreviewResourceEvent::GetEventId())
	{
		const PreviewResourceEvent& test = static_cast<const PreviewResourceEvent&>(evt);
		ViewTexture = test.Subject;
		return true;
	}
	return false;
}

#endif
