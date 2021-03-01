#include "Havana.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include "Engine/World.h"
#include "Components/Transform.h"
#include <stack>
#include "Components/Camera.h"
#include "ECS/Core.h"
#include "Engine/Engine.h"
#include "HavanaEvents.h"
#include "Events/SceneEvents.h"
#include <string>
#include <iostream>
#include "Graphics/RenderTexture.h"
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
#include <winuser.h>
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

int profilerSize = 10.f;
const int kMinProfilerSize = 10.f;
const int kMaxProfilerSize = 40.f;

Havana::Havana(Engine* GameEngine, EditorApp* app)
	: m_engine(GameEngine)
	, m_app(app)
	, CurrentDirectory("Assets")
	, m_assetBrowser(Path("Assets").FullPath, std::chrono::milliseconds(300))
{
	InitUI();
	m_assetBrowser.Start([](const std::string& path_to_watch, FileStatus status) -> void {
		// Process only regular files, all other file types are ignored
		if (!std::filesystem::is_regular_file(std::filesystem::path(path_to_watch)) && status != FileStatus::Deleted)
		{
			return;
		}

		switch (status) {
		case FileStatus::Created:
		{
			CLog::GetInstance().Log(CLog::LogType::Info, "File created: " + path_to_watch);
			TestEditorEvent evt;
			evt.Path = std::move(path_to_watch);
			evt.Queue();
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

	Icons["Close"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Close.png"));
	Icons["BugReport"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/BugReport.png"));
	Icons["Maximize"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Maximize.png"));
	Icons["ExitMaximize"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/ExitMaximize.png"));
	Icons["Minimize"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Minimize.png"));
	Icons["Play"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Play.png"));
	Icons["Pause"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Pause.png"));
	Icons["Stop"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Stop.png"));
	Icons["Info"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Info.png"));
	Icons["Logo"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/ME-LOGO.png"));
	Icons["Profiler"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Profiler.png"));
	Icons["FlipCamera"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/FlipCamera.png"));

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
}

bool show_demo_window = true;
bool show_dockspace = true;
void Havana::NewFrame(std::function<void()> StartGameFunc, std::function<void()> PauseGameFunc, std::function<void()> StopGameFunc)
{
	m_input.Update();
	OPTICK_EVENT("Havana::NewFrame");

	ImGuiIO& io = ImGui::GetIO();

	// Output size fix?
	RenderSize = Vector2(io.DisplaySize.x, io.DisplaySize.y);
	//Renderer->GetDevice().SetOutputSize(RenderSize);

	DrawMainMenuBar(StartGameFunc, PauseGameFunc, StopGameFunc);
	DrawOpenFilePopup();

	static float f = 0.0f;
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	MainMenuSize.x = 0.f;
	MainMenuSize.y = 17.f;
	DockSize = viewport->Size;
	DockSize.y = viewport->Size.y - MainMenuSize.y - kMinProfilerSize - 2;
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
	ImGui::Begin("DockSpace Demo", &show_dockspace, window_flags);
	ImGui::PopStyleVar(3);
	ImGuiID dockspace_id = ImGui::GetID("MyDockspace");
	ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;
	ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
	ImGui::ShowDemoWindow(&show_demo_window);

	ImGui::End();

	DrawLog();
	m_assetBrowser.Draw();

	//DrawCommandPanel();
	DrawResourceMonitor();
}

void Havana::DrawOpenFilePopup()
{
	OPTICK_CATEGORY("File Popup", Optick::Category::Debug);

	if (OpenScene)
	{
		ImGui::OpenPopup("Open Scene");
	}
	if (ImGui::BeginPopupModal("Open Scene", &OpenScene, ImGuiWindowFlags_MenuBar))
	{
		BrowseDirectory(Path("Assets"));

		for (auto& j : AssetDirectory)
		{
			Path path(j);
			ImGui::TreeNode(path.LocalPath.c_str());
			if (ImGui::IsItemClicked())
			{
				if (!j.is_string())
				{
					return;
				}
				LoadSceneEvent evt;
				evt.Level = path.LocalPath;
				evt.Fire();

				GetEngine().GetConfig().SetValue(std::string("CurrentScene"), GetEngine().CurrentScene->FilePath.LocalPath);

				OpenScene = false;
				ImGui::CloseCurrentPopup();
			}
		}

		if (ImGui::Button("Close"))
		{
			OpenScene = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void RecusiveDelete(EntityHandle ent, Transform* trans)
{
	if (!trans)
	{
		return;
	}
	for (auto child : trans->GetChildren())
	{
		RecusiveDelete(child->Parent, child.get());
	}
	ent->MarkForDelete();
};

void Havana::DrawMainMenuBar(std::function<void()> StartGameFunc, std::function<void()> PauseGameFunc, std::function<void()> StopGameFunc)
{
	OPTICK_CATEGORY("Main Menu Bar", Optick::Category::Debug);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 12.f));
	if (ImGui::BeginMainMenuBar())
	{
		MainMenuSize = ImGui::GetWindowSize();
		//MainMenuSize.y = 25.f;
		ImGui::SetWindowSize(MainMenuSize);
		ImGui::PopStyleVar(1);
		//ImGui::Image(Icons["Logo"]->ShaderResourceView, ImVec2(35, 35));
		if (ImGui::BeginMenu("File"))
		{
			//ImGui::MenuItem("(dummy menu)", NULL, false, false);
			if (ImGui::MenuItem("New Scene"))
			{
				NewSceneEvent evt;
				evt.Queue();
			}
			if (ImGui::MenuItem("Open Scene", "Ctrl+O"))
			{
				OpenScene = true;
			}

			if (ImGui::BeginMenu("Open Recent"))
			{
				ImGui::MenuItem("fish_hat.c");
				ImGui::MenuItem("fish_hat.inl");
				ImGui::MenuItem("fish_hat.h");
				if (ImGui::BeginMenu("More.."))
				{
					ImGui::MenuItem("Hello");
					ImGui::MenuItem("Sailor");
					if (ImGui::BeginMenu("Recurse.."))
					{
						//ShowExampleMenuFile();
						ImGui::EndMenu();
					}
					ImGui::EndMenu();
				}
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Save", "Ctrl+S"))
			{
				if (m_engine->CurrentScene/* && !std::filesystem::exists(m_engine->CurrentScene->Path.FullPath)*/)
				{
					SaveSceneEvent evt;
					//ImGui::OpenPopup("help_popup");
					evt.Fire();
				}

			}
			if (ImGui::MenuItem("Save As..")) {}
			ImGui::Separator();
			if (ImGui::BeginMenu("Options"))
			{
				static bool enabled = true;
				ImGui::MenuItem("Enabled", "", &enabled);
				ImGui::BeginChild("child", ImVec2(0, 60), true);
				for (int i = 0; i < 10; i++)
					ImGui::Text("Scrolling Text %d", i);
				ImGui::EndChild();
				static float f = 0.5f;
				static int n = 0;
				static bool b = true;
				ImGui::SliderFloat("Value", &f, 0.0f, 1.0f);
				ImGui::InputFloat("Input", &f, 0.1f);
				ImGui::Combo("Combo", &n, "Yes\0No\0Maybe\0\0");
				ImGui::Checkbox("Check", &b);
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Quit", "Alt+F4")) {}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Edit"))
		{
			const bool canUndo = EditorCommands.CanUndo();

			if (ImGui::MenuItem("Undo", "CTRL+Z", false, canUndo))
			{
				EditorCommands.Undo();
			}

			const bool canRedo = EditorCommands.CanRedo();
			if (ImGui::MenuItem("Redo", "CTRL+Y", false, canRedo))
			{
				EditorCommands.Redo();
			}

			/*ImGui::Separator();
			if (ImGui::MenuItem("Cut", "CTRL+X")) {}
			if (ImGui::MenuItem("Copy", "CTRL+C")) {}
			if (ImGui::MenuItem("Paste", "CTRL+V")) {}*/
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Add"))
		{
			if (ImGui::MenuItem("Entity"))
			{
				// The fuck is this garbage
				CreateEntity* cmd = new CreateEntity();
				EditorCommands.Push(cmd);

				AddComponentCommand* compCmd = new AddComponentCommand("Transform", cmd->Ent);
				EditorCommands.Push(compCmd);

				Transform* transform = static_cast<Transform*>(compCmd->GetComponent());
				transform->SetName("New Entity");
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("View"))
		{
			if (ImGui::MenuItem("Resource Monitor"))
			{
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Help"))
		{
			if (ImGui::MenuItem("Show ImGui Demo"))
			{
				show_demo_window = !show_demo_window;
			}
			ImGui::EndMenu();
		}
		/*if (ImGui::BeginMenu("Render Mode"))
		{
			{
				bool selected = m_engine->GetRenderer().GetContextType() == Moonlight::eDeviceContextType::Legacy;
				if (ImGui::MenuItem("Legacy", nullptr, &selected))
				{
					m_engine->GetRenderer().SetContextType(Moonlight::eDeviceContextType::Legacy);
				}
			}
			{
				bool selected = m_engine->GetRenderer().GetContextType() == Moonlight::eDeviceContextType::Immediate;
				if (ImGui::MenuItem("Immediate", nullptr, &selected))
				{
					m_engine->GetRenderer().SetContextType(Moonlight::eDeviceContextType::Immediate);
				}
			}
			{
				bool selected = m_engine->GetRenderer().GetContextType() == Moonlight::eDeviceContextType::MT_DefferedChunk;
				if (ImGui::MenuItem("Multithreaded Deferred Chunk", nullptr, &selected))
				{
					m_engine->GetRenderer().SetContextType(Moonlight::eDeviceContextType::MT_DefferedChunk);
				}
			}
			ImGui::EndMenu();
		}*/
		Input& editorInput = GetInput();
		if (!m_app->IsGameRunning())
		{
			if (ImGui::ImageButton(Icons["Play"]->TexHandle, ImVec2(30.f, 30.f)) || editorInput.IsKeyDown(KeyCode::F5))
			{
				ImGui::SetWindowFocus("Game");
				m_engine->GetInput().Resume();
				StartGameFunc();
			}
		}

		if (m_app->IsGameRunning())
		{
			//if (ImGui::ImageButton(Icons["Pause"]->ShaderResourceView, ImVec2(30.f, 30.f)) || Keyboard.F10)
			//{
			//	m_engine->GetInput().Pause();
			//	PauseGameFunc();
			//}

			if (ImGui::ImageButton(Icons["Stop"]->TexHandle, ImVec2(30.f, 30.f)) || (editorInput.IsKeyDown(KeyCode::F5) && editorInput.IsKeyDown(KeyCode::LeftShift)))
			{
				MaximizeOnPlay = false;
				SelectedTransform = nullptr;
				StopGameFunc();
				m_engine->GetInput().Stop();
			}
		}

		if (ImGui::ImageButton(Icons["FlipCamera"]->TexHandle, ImVec2(30.f, 30.f))/* || (Keyboard.F5 && Keyboard.LeftShift)*/)
		{
			if (m_viewportMode == ViewportMode::Editor)
			{
				SetViewportMode(ViewportMode::Game);
			}
			else
			{
				SetViewportMode(ViewportMode::Editor);
			}
		}

		ImGui::Image(Icons["FlipCamera"]->TexHandle, ImVec2(36.f, 36.f));

		float endOfMenu = ImGui::GetCursorPosX();
		float buttonWidth = 40.f;
		TitleBarDragPosition = Vector2(endOfMenu, 10.f);
		float winWidth = ImGui::GetWindowWidth();
		TitleBarDragSize = Vector2(winWidth - endOfMenu - (buttonWidth * 5.f), MainMenuSize.y - 10.f);

		auto window = static_cast<EditorWindow*>(m_engine->GetWindow());

		window->SetDragBounds(TitleBarDragPosition, TitleBarDragSize);

		Vector2 pos = GetInput().GetMousePosition();
		if (ImGui::IsMouseDoubleClicked(0) && (pos > TitleBarDragPosition && pos < TitleBarDragPosition + TitleBarDragSize))
		{
			GetEngine().GetWindow()->Maximize();
		}

		static int frameCount = 0;
		static float frametime = 0.0f;
		//static std::vector<float> frames;
		//frames.resize(50, GetEngine().DeltaTime * 100);
		//if (frameCount > 15)
		//{
		//	frametime = GetEngine().DeltaTime;
		//	frameCount = 0;
		//}
		//else
		//{
		//}

		// increase the counter by one
		static int m_fpscount = 0;
		static int fps = 0;
		m_fpscount++;
		++frameCount;

		static float fpsTime = 0;
		fpsTime += GetEngine().DeltaTime;
		// one second elapsed? (= 1000 milliseconds)
		if (fpsTime >= 1.f)
		{
			frametime = GetEngine().DeltaTime;
			frameCount = 0;

			// save the current counter value to m_fps
			fps = m_fpscount;

			// reset the counter and the interval
			m_fpscount = 0;
			fpsTime -= 1.f;
		}

		ImGui::Text("%.1f ms", frametime * 1000.f);
		ImGui::Text("%.1f fps", (float)fps);

		ImGui::SetCursorPosX((ImGui::GetWindowWidth() / 2.f) - (ImGui::CalcTextSize(WindowTitle.c_str()).x / 2.f));
		ImGui::Text(WindowTitle.c_str());

		ImGui::BeginGroup();
		ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.0f, 0.6f, 0.6f, 0.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0.f, 0.8f, 0.8f, 0.f));
		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (buttonWidth * 5.f));
		if (ImGui::ImageButton(Icons["Profiler"]->TexHandle, ImVec2(30.f, 30.f)))
		{
			Path optickPath = Path("Engine/Tools/");
			// additional information
			STARTUPINFO si;
			PROCESS_INFORMATION pi;

			// set the size of the structures
			ZeroMemory(&si, sizeof(si));
			si.cb = sizeof(si);
			ZeroMemory(&pi, sizeof(pi));

			// start the program up
			CreateProcess(StringUtils::ToWString(optickPath.FullPath + "Optick.exe").c_str(),   // the path
				L"",        // Command line
				NULL,           // Process handle not inheritable
				NULL,           // Thread handle not inheritable
				FALSE,          // Set handle inheritance to FALSE
				0,              // No creation flags
				NULL,           // Use parent's environment block
				NULL,           // Use parent's starting directory 
				&si,            // Pointer to STARTUPINFO structure
				&pi             // Pointer to PROCESS_INFORMATION structure (removed extra parentheses)
			);
			// Close process and thread handles. 
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
		}
		/*if (ImGui::ImageButton(Icons["Info"]->ShaderResourceView, ImVec2(30.f, 30.f)))
		{
			if (m_engine->CurrentScene && !std::filesystem::exists(m_engine->CurrentScene->FilePath.FullPath))
			{
				ImGui::OpenPopup("help_popup");
			}
		}

		if (ImGui::BeginPopup("help_popup"))
		{
			ImGui::Text("Components");
			ImGui::Separator();

			ComponentRegistry& reg = GetComponentRegistry();

			for (auto& thing : reg)
			{
				if (ImGui::Selectable(thing.first.c_str()))
				{
					if (SelectedEntity)
					{
						AddComponentCommand* compCmd = new AddComponentCommand(thing.first, SelectedEntity);
						EditorCommands.Push(compCmd);
					}
					if (SelectedTransform)
					{
						AddComponentCommand* compCmd = new AddComponentCommand(thing.first, SelectedTransform->Parent);
						EditorCommands.Push(compCmd);
					}
				}
			}
			ImGui::EndPopup();
		}*/
		float RightShift = 2.f;
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.35f, 126.f, 43.f, 1.f));
		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (buttonWidth * 4.f));
		//if (ImGui::ImageButton(Icons["BugReport"]->ShaderResourceView, ImVec2(30.f, 30.f)))
		//{
		//	ShellExecute(0, 0, L"https://github.com/wobbier/MitchEngine/issues", 0, 0, SW_SHOW);
		//}
		ImGui::PopStyleColor(1);

		//ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (buttonWidth * 3.f) + RightShift);
		//if (ImGui::ImageButton(Icons["Minimize"]->ShaderResourceView, ImVec2(30.f, 30.f)))
		//{
		//	m_engine->GetWindow()->Minimize();
		//}

		//if (static_cast<Win32Window*>(m_engine->GetWindow())->IsMaximized())
		//{
		//	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (buttonWidth * 2.f) + RightShift);
		//	if (ImGui::ImageButton(Icons["ExitMaximize"]->ShaderResourceView, ImVec2(30.f, 30.f)))
		//	{
		//		m_engine->GetWindow()->ExitMaximize();
		//	}
		//}
		//else
		//{
			ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (buttonWidth * 2.f) + RightShift);
			if (ImGui::ImageButton(Icons["Maximize"]->TexHandle, ImVec2(30.f, 30.f)))
			{
				m_engine->GetWindow()->Maximize();
			}
		//}

		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(1.f, 42.f, 43.f, 1.f));
		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth + RightShift);
		//ImGui::SameLine(0.f);
		if (ImGui::ImageButton(Icons["Close"]->TexHandle, ImVec2(30.f, 30.f)))
		{
			m_engine->GetWindow()->Exit();
		}
		ImGui::PopStyleColor(3);
		ImGui::EndGroup();

		ImGui::EndMainMenuBar();
	}
}

void Havana::DrawLog()
{
	OPTICK_CATEGORY("Log", Optick::Category::Debug);
	static std::unordered_map<CLog::LogType, bool> DebugFilters;
	ImGuiWindowFlags window_flags = 0;
	window_flags |= ImGuiWindowFlags_MenuBar;
	window_flags |= ImGuiWindowFlags_NoScrollbar;
	bool showLog = true;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1.f, 0.f));
	ImGui::Begin("Log", &showLog, window_flags);
	{
		// Menu
		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("Show"))
			{
				ImGui::Checkbox("Debug", &DebugFilters[CLog::LogType::Debug]);
				ImGui::Checkbox("Error", &DebugFilters[CLog::LogType::Error]);
				ImGui::Checkbox("Info", &DebugFilters[CLog::LogType::Info]);
				ImGui::EndMenu();
			}

			if (ImGui::Button("Clear Log"))
			{
				CLog::Messages.clear();
			}

			ImGui::EndMenuBar();
		}

		ImGuiListClipper clipper;
		clipper.Begin(CLog::Messages.size(), 12.f);

		static float currentHeight = 0.f;
		ImVec2 size = ImVec2(0, (ImGui::GetWindowSize().y - currentHeight) - ImGui::GetCursorPosY());
		static ImGuiTableFlags flags = ImGuiTableFlags_ScrollY /*| ImGuiTableFlags_ScrollFreezeTopRow*/ | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable;

		const int COLUMNS_COUNT = 1;
		static int selectedIndex = -1;
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
		if (ImGui::BeginTable("##table1ss", COLUMNS_COUNT, flags, size))
		{
			ImGui::PopStyleVar(1);

			ImGui::TableSetupColumn("Message");

			// Dummy entire-column selection storage
			// FIXME: It would be nice to actually demonstrate full-featured selection using those checkbox.
			static bool column_selected[1] = {};

			// Instead of calling TableAutoHeaders() we'll submit custom headers ourselves
			ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
			for (int column = 0; column < COLUMNS_COUNT; column++)
			{
				ImGui::TableSetColumnIndex(column);
				const char* column_name = ImGui::TableGetColumnName(column); // Retrieve name passed to TableSetupColumn()
				ImGui::PushID(column);
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
				ImGui::Checkbox("##checkall1", &column_selected[column]);
				ImGui::PopStyleVar();
				ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
				ImGui::TableHeader(column_name);
				ImGui::PopID();
			}
			while (clipper.Step())
			{
				for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
				{
					ImGui::TableNextRow(0, 12.f);
					for (int column = 0; column < 1; column++)
					{
						ImGui::TableSetColumnIndex(column);
						if (ImGui::Selectable(CLog::Messages[line_no].Message.c_str(), column_selected[column], 0, ImVec2(0.f, 12.f)))
						{
							selectedIndex = line_no;
						}
					}
				}
			}
			ImGui::EndTable();
		}
		else
		{
			ImGui::PopStyleVar(1);
		}
		clipper.End();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.f, 5.f));
		if (selectedIndex >= 0 && selectedIndex < CLog::Messages.size())
		{
			ImGuiWindowFlags window_flags = ImGuiWindowFlags_HorizontalScrollbar;
			ImGui::BeginChild("ChildL", ImVec2(ImGui::GetWindowContentRegionWidth(), 150), false, window_flags);
			ImGui::PushTextWrapPos(ImGui::GetWindowContentRegionWidth() - 10.f);
			ImGui::TextWrapped(CLog::Messages[selectedIndex].Message.c_str());
			ImGui::PopTextWrapPos();
			currentHeight = ImGui::GetWindowHeight();
			ImGui::EndChild();
		}
		ImGui::PopStyleVar(2);
		//bool showMessage = true;
		//for (auto& msg : Logger::Messages)
		//{
		//	if (DebugFilters.find(msg.Type) == DebugFilters.end())
		//	{
		//		DebugFilters[msg.Type] = true;
		//	}
		//	if (DebugFilters[msg.Type])
		//	{
		//		ImGui::Text(msg.Message.c_str());
		//	}
		//}
	}

	ImGui::End();
}

void Havana::AddComponentPopup()
{
	ImGui::PushItemWidth(-100);
	if (ImGui::Button("Add Component.."))
	{
		ImGui::OpenPopup("my_select_popup");
	}
	ImGui::PopItemWidth();

	if (ImGui::BeginPopup("my_select_popup"))
	{
		DrawAddComponentList(SelectedEntity);

		ImGui::EndPopup();
	}
}

void Havana::DoComponentRecursive(const FolderTest& currentFolder, const EntityHandle& entity)
{
	for (auto& entry : currentFolder.Folders)
	{
		if (ImGui::BeginMenu(entry.first.c_str()))
		{
			DoComponentRecursive(entry.second, entity);
			ImGui::EndMenu();
		}
	}
	for (auto& ptr : currentFolder.Reg)
	{
		if (ImGui::Selectable(ptr.first.c_str()))
		{
			if (entity)
			{
				AddComponentCommand* compCmd = new AddComponentCommand(ptr.first, entity);
				EditorCommands.Push(compCmd);
			}
			/*if (SelectedTransform)
			{
				m_engine->GetWorld().lock()->GetEntity(SelectedTransform->Parent).lock()->AddComponentByName(thing.first);
			}*/
		}
	}
}


void Havana::DrawAddComponentList(const EntityHandle& entity)
{
	ImGui::Text("Components");
	ImGui::Separator();
	std::map<std::string, FolderTest> folders;
	ComponentRegistry& reg = GetComponentRegistry();

	for (auto& thing : reg)
	{
		if (thing.second.Folder == "")
		{
			folders[""].Reg[thing.first] = &thing.second;
		}
		else
		{
			/*auto it = folders.at(thing.second.Folder);
			if (it == folders.end())
			{

			}*/
			std::string folderPath = thing.second.Folder;
			std::size_t pos = folderPath.rfind("/");
			if (pos == std::string::npos)
			{
				folders[thing.second.Folder].Reg[thing.first] = &thing.second;
			}
			else
			{
				FolderTest& test = folders[thing.second.Folder.substr(0, pos)];
				while (pos != std::string::npos)
				{
					pos = folderPath.rfind("/");
					if (pos == std::string::npos)
					{
						test.Folders[folderPath].Reg[thing.first] = &thing.second;
					}
					else
					{
						test = folders[folderPath.substr(0, pos)];
						folderPath = folderPath.substr(pos + 1, folderPath.size());
					}
				}
			}
		}
	}

	for (auto& thing : folders)
	{
		if (thing.first != "")
		{
			if (ImGui::BeginMenu(thing.first.c_str()))
			{
				DoComponentRecursive(thing.second, entity);
				ImGui::EndMenu();
			}
		}
		else
		{
			for (auto& ptr : thing.second.Reg)
			{
				if (ImGui::Selectable(ptr.first.c_str()))
				{
					if (entity)
					{
						AddComponentCommand* compCmd = new AddComponentCommand(ptr.first, entity);
						EditorCommands.Push(compCmd);
					}
					/*if (SelectedTransform)
					{
						m_engine->GetWorld().lock()->GetEntity(SelectedTransform->Parent).lock()->AddComponentByName(thing.first);
					}*/
				}
			}
		}
	}

	//for (auto& thing : reg)
	//{
	//	if (ImGui::Selectable(thing.first.c_str()))
	//	{
	//		if (entity)
	//		{
	//			AddComponentCommand* compCmd = new AddComponentCommand(thing.first, entity);
	//			EditorCommands.Push(compCmd);
	//		}
	//		/*if (SelectedTransform)
	//		{
	//			m_engine->GetWorld().lock()->GetEntity(SelectedTransform->Parent).lock()->AddComponentByName(thing.first);
	//		}*/
	//	}
	//}
}

void Havana::DrawAddCoreList()
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

void Havana::DrawResourceMonitor()
{
	OPTICK_CATEGORY("Resource Monitor", Optick::Category::Debug);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
	ImGui::Begin("Resource Monitor");
	{
		auto& resources = ResourceCache::GetInstance().GetResouceStack();
		std::vector<std::shared_ptr<Resource>> resourceList;
		resourceList.reserve(resources.size());

		for (auto resource : resources)
		{
			resourceList.push_back(resource.second);
		}

		ImVec2 size = ImVec2(0, ImGui::GetWindowSize().y - ImGui::GetCursorPosY());
		static ImGuiTableFlags flags = ImGuiTableFlags_ScrollY /*| ImGuiTableFlags_ScrollFreezeTopRow*/ | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable;

		if (ImGui::BeginTable("##ResourceTable", 2, flags, size))
		{
			ImGui::TableSetupColumn("Resource Path", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("References", ImGuiTableColumnFlags_WidthFixed);
			//ImGui::TableAutoHeaders();
			ImGuiListClipper clipper;
			clipper.Begin(resourceList.size());
			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
				{
					ImGui::TableNextRow();

					ImGui::TableSetColumnIndex(0);
					ImGui::Text(resourceList[row]->GetPath().LocalPath.c_str());
					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();

						if (auto metaData = resourceList[row]->GetMetadata())
						{
							metaData->OnEditorInspect();
						}

						ImGui::EndTooltip();
					}

					ImGui::TableSetColumnIndex(1);
					ImGui::Text(std::to_string(resourceList[row].use_count()).c_str());

				}
			}
			ImGui::EndTable();
		}
		ImGui::End();
	}
	ImGui::PopStyleVar(1);
}

void Havana::UpdateWorld(World* world, Transform* root, const std::vector<Entity>& ents)
{
	OPTICK_CATEGORY("Havana::UpdateWorld", Optick::Category::GameLogic);
	m_rootTransform = root;
	ImGui::Begin("World", 0, ImGuiWindowFlags_MenuBar);
	ImGui::BeginMenuBar();
	if (ImGui::BeginMenu("Create"))
	{
		if (ImGui::BeginMenu("Entity"))
		{
			if (ImGui::IsItemClicked())
			{
				SelectedEntity = m_engine->GetWorld().lock()->CreateEntity();
			}
			if (ImGui::MenuItem("Entity Transform"))
			{
				SelectedEntity = m_engine->GetWorld().lock()->CreateEntity();
				SelectedEntity->AddComponent<Transform>("New Entity");
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Core"))
		{
			DrawAddCoreList();
			ImGui::EndMenu();
		}
		ImGui::EndMenu();
	}
	ImGui::EndMenuBar();
	if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen))
	{
		OPTICK_CATEGORY("Entity List", Optick::Category::GameLogic);
		if (ImGui::IsWindowFocused())
		{
			if (SelectedTransform && GetInput().IsKeyDown(KeyCode::Delete))
			{
				RecusiveDelete(SelectedTransform->Parent, SelectedTransform);
				ClearSelection();
			}
		}
		UpdateWorldRecursive(root);
	}
	if (ents.size() > 0)
	{
		if (ImGui::CollapsingHeader("Utility", ImGuiTreeNodeFlags_DefaultOpen))
		{
			OPTICK_CATEGORY("Utility Entities", Optick::Category::Debug);
			int i = 0;
			for (const Entity& ent : ents)
			{
				for (BaseComponent* comp : ent.GetAllComponents())
				{
					ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | (SelectedEntity.Get() == &ent ? ImGuiTreeNodeFlags_Selected : 0);
					{
						node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen; // ImGuiTreeNodeFlags_Bullet
						ImGui::TreeNodeEx((void*)(intptr_t)i, node_flags, comp->GetName().c_str());
						if (ImGui::IsItemClicked())
						{
							SelectedCore = nullptr;
							SelectedTransform = nullptr;
							SelectedEntity = EntityHandle(ent.GetId(), world->GetSharedPtr());
						}
					}
				}
			}
		}
	}

	if (ImGui::CollapsingHeader("Entity Cores", ImGuiTreeNodeFlags_DefaultOpen))
	{
		OPTICK_CATEGORY("Entity Cores", Optick::Category::Debug);
		int i = 0;
		for (auto& comp : world->GetAllCoresArray())
		{
			ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | (SelectedCore == comp.second.get() ? ImGuiTreeNodeFlags_Selected : 0);
			{
				node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen; // ImGuiTreeNodeFlags_Bullet
				ImGui::TreeNodeEx((void*)(intptr_t)i, node_flags, comp.second->GetName().c_str());
				if (ImGui::IsItemClicked())
				{
					SelectedCore = comp.second.get();
					SelectedTransform = nullptr;
					SelectedEntity = EntityHandle();
				}
			}
		}
	}
	ImGui::End();

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
		AddComponentPopup();
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

void Havana::UpdateWorldRecursive(Transform* root)
{
	OPTICK_CATEGORY("UpdateWorld::UpdateWorldRecursive", Optick::Category::GameLogic);
	if (!root)
		return;
	if (ImGui::BeginDragDropTarget())
	{
		HandleAssetDragAndDrop(root);

		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_CHILD_TRANSFORM"))
		{
			DragParentDescriptor.Parent->SetParent(*root);
		}
		ImGui::EndDragDropTarget();
	}

	int i = 0;
	for (SharedPtr<Transform> child : root->GetChildren())
	{
		OPTICK_CATEGORY("UpdateWorld::UpdateWorldRecursive::Child", Optick::Category::GameLogic);
		if (!child)
		{
			continue;
		}
		Transform* var = child.get();
		bool open = false;
		ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | (SelectedTransform == var ? ImGuiTreeNodeFlags_Selected : 0);
		if (var->GetChildren().empty())
		{
			node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen; // ImGuiTreeNodeFlags_Bullet
			open = ImGui::TreeNodeEx((void*)(intptr_t)i, node_flags, var->Name.c_str());
			if (ImGui::IsItemClicked())
			{
				SelectedTransform = var;
				SelectedCore = nullptr;
				SelectedEntity = SelectedTransform->Parent;
			}

			DrawEntityRightClickMenu(var);

			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
			{
				//files.FullPath = dir.FullPath;
				DragParentDescriptor.Parent = var;
				ImGui::SetDragDropPayload("DND_CHILD_TRANSFORM", &DragParentDescriptor, sizeof(ParentDescriptor));
				ImGui::Text(var->GetName().c_str());
				ImGui::EndDragDropSource();
			}

			if (ImGui::BeginDragDropTarget())
			{
				HandleAssetDragAndDrop(var);

				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_CHILD_TRANSFORM"))
				{
					DragParentDescriptor.Parent->SetParent(*child);
				}
				ImGui::EndDragDropTarget();
			}

			//if (open)
			//{
			//	// your tree code stuff
			//	ImGui::TreePop();
			//}
		}
		else
		{
			OPTICK_CATEGORY("UpdateWorld::UpdateWorldRecursive::Leaf", Optick::Category::GameLogic);
			open = ImGui::TreeNodeEx((void*)(intptr_t)i, node_flags, var->Name.c_str());
			if (ImGui::IsItemClicked())
			{
				SelectedTransform = var;
				SelectedCore = nullptr;
				SelectedEntity = SelectedTransform->Parent;
			}

			DrawEntityRightClickMenu(var);

			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
			{
				DragParentDescriptor.Parent = var;
				//files.FullPath = dir.FullPath;
				ImGui::SetDragDropPayload("DND_CHILD_TRANSFORM", &DragParentDescriptor, sizeof(ParentDescriptor));
				ImGui::Text(var->GetName().c_str());
				ImGui::EndDragDropSource();
			}

			if (ImGui::BeginDragDropTarget())
			{
				HandleAssetDragAndDrop(var);

				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_CHILD_TRANSFORM"))
				{
					DragParentDescriptor.Parent->SetParent(*child);
				}
				ImGui::EndDragDropTarget();
			}

			if (open)
			{
				UpdateWorldRecursive(var);
				ImGui::TreePop();
				//ImGui::TreePop();
			}
		}

		i++;
	}
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
	return m_input;
}

void Havana::DrawEntityRightClickMenu(Transform* transform)
{
	if (ImGui::BeginPopupContextItem())
	{
		if (ImGui::MenuItem("Add Child"))
		{
			EntityHandle ent = GetEngine().GetWorld().lock()->CreateEntity();
			ent->AddComponent<Transform>().SetParent(*transform);
			GetEngine().GetWorld().lock()->Simulate();
		}

		if (ImGui::MenuItem("Delete", "Del"))
		{
			RecusiveDelete(transform->Parent, transform);
			GetEngine().GetWorld().lock()->Simulate();
			ClearSelection();
		}
		if (ImGui::BeginMenu("Add Component"))
		{
			DrawAddComponentList(transform->Parent);
			ImGui::EndMenu();
		}
		ImGui::EndPopup();
	}
}

void Havana::Render(Moonlight::CameraData& EditorCamera)
{
	OPTICK_EVENT("Editor Render", Optick::Category::Rendering);
	auto& io = ImGui::GetIO();

	RenderMainView(EditorCamera);

	RenderProfilerBar();


	//ImGui::Render();
	//ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	// Update and Render additional Platform Windows
}

void Havana::RenderProfilerBar()
{
	const ImU32 flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar /*| ImGuiWindowFlags_NoInputs*/ | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;
	//
	//#ifdef IMGUI_HAS_VIEWPORT
	//	ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size);
	//	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->Pos);
	//#else
	ImGui::SetNextWindowSize(ImVec2(ImGui::GetMainViewport()->Size.x, profilerSize));
	auto pos = ImGui::GetMainViewport()->Pos;
	ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y + ImGui::GetMainViewport()->Size.y - (profilerSize)));
	//#endif

	//ImGui::PushStyleColor(ImGuiCol_WindowBg, 0);
	ImGui::PushStyleColor(ImGuiCol_Border, 0);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
	//ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.f));


	static int frameCount = 0;
	static float frametime = 0.0f;

	// increase the counter by one
	static int m_fpscount = 0;
	static int fps = 0;
	m_fpscount++;
	++frameCount;

	float totalFrameTime = 0.f;
	for (auto& thing : FrameProfile::GetInstance().ProfileDump)
	{
		totalFrameTime += thing.second.Timer.GetDeltaSeconds();
	}

	//totalFrameTime = FrameProfile::GetInstance().MainDelta;

	ImGui::Begin("gizmo", NULL, flags);
	//ImDrawList* draw_list = ImGui::GetWindowDrawList();
	{

		//ImGui::Text("AYO LMAO");
		//ImGui::SetNextWindowSize(ImVec2(350, 560), ImGuiCond_FirstUseEver);

		ImDrawList* draw_list = ImGui::GetForegroundDrawList();

		const ImVec2 p = ImGui::GetCursorScreenPos();
		static ImVec4 col2 = ImVec4(1.0f, .5f, 1.0f, 1.0f);
		static ImVec4 col3 = ImVec4(.5f, 1.f, 1.0f, 1.0f);
		{
			ImU32 col322 = ImColor(col2);
			ImU32 col323 = ImColor(col3);
			float x = p.x, y = p.y;

			//float sz = 50.f;
			//draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + ImGui::GetWindowSize().x, y + sz), col32);
			float previousX = 0;
			float targetFPS = (1.f / GetEngine().FPS);
			float size = (totalFrameTime / targetFPS);

			if (size >= 100.f)
			{
				size /= 100.f;
			}

			// Full window size
			float windowSizeX = ImGui::GetMainViewport()->Size.x;

			// How big is the whole frame UI wise
			float profileSize = (windowSizeX * size);
			for (auto& thing : FrameProfile::GetInstance().ProfileDump)
			{
				Vector3 profileColor = FrameProfile::GetInstance().GetCategoryColor(thing.second.Category);
				ImVec4 col = ImVec4(profileColor.x, profileColor.y, profileColor.z, 1.0f);
				ImU32 col32 = ImColor(col);
				float maxSize = (profileSize <= windowSizeX) ? profileSize : windowSizeX ;
				float profileSizeX = (thing.second.Timer.GetDeltaSeconds() / totalFrameTime) * maxSize;
				draw_list->AddRectFilled(ImVec2(previousX + x, y), ImVec2(previousX + x + profileSizeX, y + profilerSize), col32);
				ImGui::SameLine();
				previousX = previousX + profileSizeX;
			}
			if (profileSize > windowSizeX)
			{
				ImVec4 col = ImVec4(1.f, 0.f, 0.f, 1.f);
				ImU32 col32 = ImColor(col);

				float profileSizeX =  profileSize / windowSizeX;
				float xxxx = windowSizeX * profileSizeX;
				draw_list->AddRectFilled(ImVec2(x + windowSizeX - (profileSize - windowSizeX), y), ImVec2((windowSizeX + xxxx), y + 10.f), col32);
				ImGui::SameLine();
				previousX = previousX + profileSizeX;
			}
			else
			{
				ImVec4 col = ImVec4(0.f, 1.f, 0.f, 1.f);
				ImU32 col32 = ImColor(col);
				//float xxxx = windowSizeX * profileSize;
				draw_list->AddRectFilled(ImVec2(previousX + x, y), ImVec2(windowSizeX + x, y + profilerSize), col32);
			}

			ImGui::PopStyleVar(3);
			ImGui::PopStyleColor(1);

			if (ImGui::IsWindowHovered())
			{
				profilerSize = kMaxProfilerSize;
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);

				for (auto& thing : FrameProfile::GetInstance().ProfileDump)
				{
					ImGui::TextUnformatted(thing.first.c_str());
					ImGui::SameLine();
					ImGui::TextUnformatted(std::to_string(thing.second.Timer.GetDeltaSeconds() * 1000.f).c_str());
					ImGui::SameLine();
					ImGui::TextUnformatted(" ms");
				}
				ImGui::PopTextWrapPos();

				ImGui::EndTooltip();
			}
			else
			{
				profilerSize = kMinProfilerSize;
			}
		}
	}
	ImGui::End();
}

void Havana::RenderMainView(Moonlight::CameraData& EditorCamera)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

	//ImGui::Begin("Game Depth");
	//{
	//	m_isGameFocused = ImGui::IsWindowFocused();


	//	if (Renderer->GameViewRTT && Renderer->GameViewRTT->NormalShaderResourceView != nullptr)
	//	{
	//		// Get the current cursor position (where your window is)
	//		ImVec2 pos = ImGui::GetCursorScreenPos();
	//		ImVec2 maxPos = ImVec2(pos.x + ImGui::GetWindowSize().x, pos.y + ImGui::GetWindowSize().y);
	//		//GameRenderSize = Vector2(ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);
	//		GameRenderSize = Vector2(ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);

	//		// Ask ImGui to draw it as an image:
	//		// Under OpenGL the ImGUI image type is GLuint
	//		// So make sure to use "(void *)tex" but not "&tex"
	//		ImGui::GetWindowDrawList()->AddImage(
	//			(void*)Renderer->GameViewRTT->NormalShaderResourceView.Get(),
	//			ImVec2(pos.x, pos.y),
	//			ImVec2(maxPos),
	//			ImVec2(0, 0),
	//			ImVec2(Mathf::Clamp(0.f, 1.0f, GameRenderSize.X() / Renderer->GameViewRTT->Width), Mathf::Clamp(0.f, 1.0f, GameRenderSize.Y() / Renderer->GameViewRTT->Height)));
	//		//ImVec2(WorldViewRenderSize.X() / RenderSize.X(), WorldViewRenderSize.Y() / RenderSize.Y()));

	//	}
	//}
	//ImGui::End();

	ImGuiWindowFlags window_flags = 0;
	window_flags |= ImGuiWindowFlags_MenuBar;
	bool showGameWindow = true;
	ImGui::Begin("Game", &showGameWindow, window_flags);

	if (MaximizeOnPlay)
	{
		ImGui::End();
		window_flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;
		ImGui::SetNextWindowPos(ImVec2(DockPos.x, DockPos.y + 20.f));
		ImGui::SetNextWindowSize(ImVec2(DockSize.x, DockSize.y - 20.f));
		ImGui::Begin("Full Screen Viewport", &MaximizeOnPlay, window_flags);
	}

	if (m_viewportMode == ViewportMode::Game)
	{
		if (GetInput().IsKeyDown(KeyCode::Escape)/*  && m_app->IsGameRunning()&& AllowGameInput*/)
		{
			m_engine->GetInput().Stop();
			AllowGameInput = false;
			ImGui::SetWindowFocus("World");
			//Renderer->SetViewportMode(ViewportMode::World);
		}
		else if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)/* && AllowGameInput*/)
		{
			m_engine->GetInput().Resume();
			AllowGameInput = true;
		}
	}

	if (Renderer && m_engine->EditorCamera.Buffer)
	{
	//	//AllowGameInput = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
	//	static auto srv = Renderer->GameViewRTT->ShaderResourceView;
	//	static std::string RenderTextureSceneName = "Diffuse";
	//	static std::string RenderTextureGameName = "Shaded";

	//	static std::string* RenderTextureNamePtr = &RenderTextureSceneName;
	//	if (m_viewportMode == ViewportMode::Game)
	//	{
	//		RenderTextureNamePtr = &RenderTextureGameName;
	//	}
	//	else
	//	{
	//		RenderTextureNamePtr = &RenderTextureSceneName;
	//	}
	//	std::string& RenderTextureName = *RenderTextureNamePtr;
		if (ImGui::BeginMenuBar())
		{
	//		if (ImGui::BeginMenu(RenderTextureName.c_str()))
	//		{
	//			if (ImGui::MenuItem("Shaded", "", false))
	//			{
	//				RenderTextureName = "Shaded";
	//			}
	//			if (ImGui::MenuItem("Diffuse", "", false))
	//			{
	//				RenderTextureName = "Diffuse";
	//			}
	//			if (ImGui::MenuItem("Normals", "", false))
	//			{
	//				RenderTextureName = "Normals";
	//			}
	//			if (ImGui::MenuItem("Specular", "", false))
	//			{
	//				RenderTextureName = "Specular";
	//			}
	//			if (ImGui::MenuItem("Depth", "", false))
	//			{
	//				RenderTextureName = "Depth";
	//			}

	//			if (m_viewportMode == ViewportMode::Game)
	//			{
	//				if (ImGui::MenuItem("UI", "", false))
	//				{
	//					RenderTextureName = "UI";
	//				}
	//			}

	//			if (ImGui::MenuItem("Position", "", false))
	//			{
	//				RenderTextureName = "Position";
	//			}
	//			if (ImGui::MenuItem("Shadow", "", false))
	//			{
	//				RenderTextureName = "Shadow";
	//			}

	//			if (m_viewportMode == ViewportMode::World)
	//			{
	//				if (ImGui::MenuItem("Pick Mask", "", false))
	//				{
	//					RenderTextureName = "Pick Mask";
	//				}
	//			}

	//			ImGui::EndMenu();
	//		}

			ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 150.f);

			if (ImGui::Button("Toggle Fullscreen", ImVec2(150.f, 20.f)))
			{
				MaximizeOnPlay = !MaximizeOnPlay;
			}

			ImGui::EndMenuBar();
		}

	//	if (RenderTextureName == "Shaded")
	//	{
	//		srv = Renderer->GameViewRTT->ShaderResourceView;
	//	}
	//	else if (RenderTextureName == "Diffuse")
	//	{
	//		srv = Renderer->GameViewRTT->ColorShaderResourceView;
	//	}
	//	else if (RenderTextureName == "Normals")
	//	{
	//		srv = Renderer->GameViewRTT->NormalShaderResourceView;
	//	}
	//	else if (RenderTextureName == "Specular")
	//	{
	//		srv = Renderer->GameViewRTT->SpecularShaderResourceView;
	//	}
	//	else if (RenderTextureName == "Depth")
	//	{
	//		srv = Renderer->GameViewRTT->DepthShaderResourceView;
	//	}
	//	else if (RenderTextureName == "UI")
	//	{
	//		srv = Renderer->GameViewRTT->UIShaderResourceView;
	//	}
	//	else if (RenderTextureName == "Position")
	//	{
	//		srv = Renderer->GameViewRTT->PositionShaderResourceView;
	//	}
	//	else if (RenderTextureName == "Shadow")
	//	{
	//		srv = Renderer->GameViewRTT->ShadowMapShaderResourceView;
	//	}
	//	else if (RenderTextureName == "Pick Mask")
	//	{
	//		srv = Renderer->GameViewRTT->PickingResourceView;
	//	}
		m_isGameFocused = ImGui::IsWindowFocused();


	//	if (m_viewportMode == ViewportMode::Game)
	//	{
	//		if (Renderer && Renderer->GameViewRTT && srv != nullptr)
	//		{
	//			// Get the current cursor position (where your window is)
	//			ImVec2 pos = ImGui::GetCursorScreenPos();
	//			ImVec2 maxPos = ImVec2(pos.x + ImGui::GetWindowSize().x, pos.y + ImGui::GetWindowSize().y);
	//			//GameRenderSize = Vector2(ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);
	//			GameRenderSize = Vector2(ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);
	//			GameViewRenderLocation = Vector2(pos.x, pos.y);

	//			// Ask ImGui to draw it as an image:
	//			// Under OpenGL the ImGUI image type is GLuint
	//			// So make sure to use "(void *)tex" but not "&tex"
	//			ImGui::GetWindowDrawList()->AddImage(
	//				(void*)srv.Get(),
	//				ImVec2(pos.x, pos.y),
	//				ImVec2(maxPos),
	//				ImVec2(0, 0),
	//				ImVec2(Mathf::Clamp(0.f, 1.0f, GameRenderSize.X() / Renderer->GameViewRTT->Width), Mathf::Clamp(0.f, 1.0f, GameRenderSize.Y() / Renderer->GameViewRTT->Height)));
	//			//ImVec2(WorldViewRenderSize.X() / RenderSize.X(), WorldViewRenderSize.Y() / RenderSize.Y()));

	//		}
	//	}
	//	else
	//	{

			// Get the current cursor position (where your window is)
			ImVec2 pos = ImGui::GetCursorScreenPos();
			/*Vector2 evt;
			evt.SetX((GetEngine().GetWindow()->GetPosition().X() + GetInput().GetMousePosition().X()) - pos.x);
			evt.SetY((GetEngine().GetWindow()->GetPosition().Y() + GetInput().GetMousePosition().Y()) - pos.y);
			mouseTracker.Update(GetInput().GetMouseState());
			static bool hasClicked = false;*/
			/*if (Renderer && GetInput().GetMouseState().leftButton && !hasClicked)
			{
				Renderer->PickScene(evt);
				hasClicked = true;
			}
			else
			{
				hasClicked = false;
			}*/

			//DirectX::XMFLOAT4X4 objView;
			//if (SelectedTransform)
			//{
			//	DirectX::XMStoreFloat4x4(&objView, SelectedTransform->GetMatrix().GetInternalMatrix());
			//}

			//ImGuizmo::BeginFrame();
			static ImGuizmo::OPERATION mCurrentGizmoOperation(ImGuizmo::TRANSLATE);
			static ImGuizmo::MODE mCurrentGizmoMode(ImGuizmo::LOCAL);
			//static bool useSnap = false;
			//static float snap[3] = { 1.f, 1.f, 1.f };

			//float matrixTranslation[3], matrixRotation[3], matrixScale[3];
			////ImGuizmo::DecomposeMatrixToComponents(&objView._11, matrixTranslation, matrixRotation, matrixScale);

			//bool isMovingMouse = (ImGui::GetMousePos().x != previousMousePos.x) && (ImGui::GetMousePos().y != previousMousePos.y);
			//previousMousePos = ImGui::GetMousePos();
			//if (ImGui::IsWindowFocused() && ImGuizmo::IsUsing() && isMovingMouse) {
			//	//ImGui::InputFloat3("Tr", matrixTranslation, 3);
			//	if (SelectedTransform)
			//	{
			//		//HavanaUtils::EditableVector3("RtVec", SelectedTransform->Rotation);
			//		//matrixRotation[0] = SelectedTransform->Rotation[0] * DirectX::XM_PI / 180.f;
			//		//matrixRotation[1] = SelectedTransform->Rotation[1] * DirectX::XM_PI / 180.f;
			//		//matrixRotation[2] = SelectedTransform->Rotation[2] * DirectX::XM_PI / 180.f;
			//	}
			//	//else
			//	//{
			//	//	ImGui::InputFloat3("Rt", matrixRotation, 3);
			//	//}
			//	//ImGui::InputFloat3("Sc", matrixScale, 3);
			//	//ImGuizmo::RecomposeMatrixFromComponents(matrixTranslation, matrixRotation, matrixScale, &objView._11);
			//}

			{
				m_isWorldViewFocused = ImGui::IsWindowFocused();


				if (Renderer && bgfx::isValid(m_engine->EditorCamera.Buffer->Buffer))
				{
					ImVec2 maxPos = ImVec2(pos.x + ImGui::GetWindowSize().x, pos.y + ImGui::GetWindowSize().y);
					WorldViewRenderSize = Vector2(ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);
					WorldViewRenderLocation = Vector2(pos.x, pos.y);
					// Ask ImGui to draw it as an image:
					// Under OpenGL the ImGUI image type is GLuint
					// So make sure to use "(void *)tex" but not "&tex"
					ImGui::Image(bgfx::getTexture(m_engine->EditorCamera.Buffer->Buffer), ImVec2(WorldViewRenderSize.x, WorldViewRenderSize.y- 38.f), ImVec2(0, 0),
						ImVec2(Mathf::Clamp(0.f, 1.0f, WorldViewRenderSize.x / m_engine->EditorCamera.Buffer->Width), Mathf::Clamp(0.f, 1.0f, WorldViewRenderSize.y / m_engine->EditorCamera.Buffer->Height)));
					/*ImGui::GetWindowDrawList()->AddImage(
						(void*)srv.Get(),
						ImVec2(pos.x, pos.y),
						ImVec2(maxPos),
						ImVec2(0, 0),
						ImVec2(Mathf::Clamp(0.f, 1.0f, WorldViewRenderSize.X() / Renderer->GameViewRTT->Width), Mathf::Clamp(0.f, 1.0f, WorldViewRenderSize.Y() / Renderer->GameViewRTT->Height)));*/

					ImGuizmo::SetRect(WorldViewRenderLocation.x, WorldViewRenderLocation.y, WorldViewRenderSize.x, WorldViewRenderSize.y);

					//DirectX::XMFLOAT4X4 fView;
					//if (EditorCamera.Projection == Moonlight::ProjectionType::Perspective)
					//{
					//	DirectX::XMMATRIX perspectiveMatrix = DirectX::XMMatrixPerspectiveFovRH(
					//		EditorCamera.FOV * DirectX::XM_PI / 180.0f,
					//		WorldViewRenderSize.X() / WorldViewRenderSize.Y(),
					//		.1f,
					//		1000.0f
					//	);

					//	DirectX::XMStoreFloat4x4(&fView, perspectiveMatrix);
					//}
					//else
					//{
					//	DirectX::XMMATRIX perspectiveMatrix = DirectX::XMMatrixOrthographicRH(
					//		EditorCamera.OutputSize.X() / EditorCamera.OrthographicSize,
					//		EditorCamera.OutputSize.Y() / EditorCamera.OrthographicSize,
					//		.1f,
					//		100.0f
					//	);

					//	DirectX::XMStoreFloat4x4(&fView, perspectiveMatrix);
					//}
					//ImGuizmo::SetDrawlist();
					//ImGuizmo::SetOrthographic(false);

					//const DirectX::XMVECTORF32 eye = { EditorCamera.Position.x, EditorCamera.Position.y, EditorCamera.Position.z, 0 };
					//const DirectX::XMVECTORF32 at = { EditorCamera.Position.x + EditorCamera.Front.x, EditorCamera.Position.y + EditorCamera.Front.y, EditorCamera.Position.z + EditorCamera.Front.z, 0.0f };
					//const DirectX::XMVECTORF32 up = { Vector3::Up.x, Vector3::Up.y, Vector3::Up.z, 0 };

					//DirectX::XMMATRIX vec = DirectX::XMMatrixLookAtRH(eye, at, up);

					//DirectX::XMFLOAT4X4 lookAtView;
					//DirectX::XMStoreFloat4x4(&lookAtView, vec);

					//DirectX::XMFLOAT4X4 idView;
					//DirectX::XMStoreFloat4x4(&idView, DirectX::XMMatrixIdentity());

					////ImGuizmo::DrawGrid(&fView2._11, &fView._11, &idView._11, 100.f);
					////ImGuizmo::DrawCubes(&fView2._11, &fView._11, &idView._11, 1);
					//ImGuizmo::Manipulate(&lookAtView._11, &fView._11, mCurrentGizmoOperation, mCurrentGizmoMode, &objView._11, &idView._11, useSnap ? &snap[0] : NULL);
					//if (ImGui::IsWindowFocused() && ImGuizmo::IsUsing()/* && isMovingMouse*/)
					//{
					//	if (SelectedTransform)
					//	{
					//		//ImGuizmo::DecomposeMatrixToComponents(&objView._11, matrixTranslation, matrixRotation, matrixScale);

					//		Matrix4 m = Matrix4(objView);

					//		DirectX::SimpleMath::Vector3 pos;
					//		DirectX::SimpleMath::Quaternion rot;
					//		DirectX::SimpleMath::Vector3 scale;
					//		m.GetInternalMatrix().Decompose(scale, rot, pos);

					//		//memcpy(matrixTranslation, prevMatrixTranslation, sizeof(float) * 3);
					//		//memcpy(matrixRotation, prevMatrixRotation, sizeof(float) * 3);
					//		prevMatrixRotation[0] = matrixRotation[0];
					//		prevMatrixRotation[1] = matrixRotation[1];
					//		prevMatrixRotation[2] = matrixRotation[2];

					//		Quaternion q = rot;// DirectX::SimpleMath::Quaternion::CreateFromRotationMatrix(objView);
					//		//memcpy(matrixScale, prevMatrixScale, sizeof(float) * 3);
					//		//SelectedTransform->SetWorldTransform(m, false);
					//		Vector3 euler = Quaternion::ToEulerAngles(q);
					//		SelectedTransform->SetPosition(Vector3(pos));
					//		//SelectedTransform->SetRotation(euler);// Vector3(matrixRotation[0], matrixRotation[1], matrixRotation[2]));
					//		//SelectedTransform->SetRotation(Vector3(euler.x * 180.f / DirectX::XM_PI, euler.y * 180.f / DirectX::XM_PI, euler .z * 180.f / DirectX::XM_PI));

					//		SelectedTransform->SetScale(Vector3(scale));

					//	}
					//}

					//{

					//ImGuizmo::ViewManipulate(&lookAtView._11, 8.f, ImVec2(WorldViewRenderLocation.X() + WorldViewRenderSize.X() - 128, WorldViewRenderLocation.Y()), ImVec2(128, 128), 0x00101010);

					////DirectX::SimpleMath::Vector3 pos;
					////DirectX::SimpleMath::Quaternion rot;
					////DirectX::SimpleMath::Vector3 scale;
					////Matrix4(lookAtView).GetInternalMatrix().Decompose(scale, rot, pos);

					////m_app->EditorSceneManager->GetEditorCameraTransform()->SetPosition(pos);
					//}
				}
			}

			ImGui::SetCursorPos(ImVec2(5.f, 45.f));
			if (!GetInput().IsMouseButtonDown(MouseButton::Right))
			{
				if (GetInput().IsKeyDown(KeyCode::W))
					mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
				if (GetInput().IsKeyDown(KeyCode::E))
					mCurrentGizmoOperation = ImGuizmo::ROTATE;
				if (GetInput().IsKeyDown(KeyCode::R))
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

			//if (ImGui::IsKeyPressed(83))
			//	useSnap = !useSnap;
			//ImGui::Checkbox("", &useSnap);
			//ImGui::SameLine();

			//switch (mCurrentGizmoOperation)
			//{
			//case ImGuizmo::TRANSLATE:
			//	ImGui::InputFloat3("Snap", &snap[0]);
			//	break;
			//case ImGuizmo::ROTATE:
			//	ImGui::InputFloat("Angle Snap", &snap[0]);
			//	break;
			//case ImGuizmo::SCALE:
			//	ImGui::InputFloat("Scale Snap", &snap[0]);
			//	break;
			//}
		//}
	}
	ImGui::End();
	ImGui::PopStyleVar(3);
}

void Havana::SetViewportMode(ViewportMode mode)
{
	m_viewportMode = mode;
}

const bool Havana::IsGameFocused() const
{
	return m_isGameFocused;
}

const bool Havana::IsWorldViewFocused() const
{
	return m_isWorldViewFocused;
}

void Havana::BrowseDirectory(const Path& path)
{
	if (CurrentDirectory.FullPath == path.FullPath && !AssetDirectory.empty())
	{
		return;
	}

	for (const auto& entry : std::filesystem::directory_iterator(path.FullPath))
	{
		Path filePath(entry.path().string());
		if (filePath.LocalPath.find(".lvl") != std::string::npos)
		{
			AssetDirectory.push_back(filePath.FullPath);
		}

		ImGui::Text(filePath.Directory.c_str());

		ImGui::Text(filePath.LocalPath.c_str());
	}
}

const Vector2& Havana::GetGameOutputSize() const
{
	return GameRenderSize;
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