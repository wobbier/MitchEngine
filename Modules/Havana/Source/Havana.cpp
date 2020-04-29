#include "Havana.h"
#include "imgui.h"
#include "examples/imgui_impl_win32.h"
#include "examples/imgui_impl_dx11.h"
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
#include "Logger.h"
#include "Resource/ResourceCache.h"
#include "Graphics/Texture.h"
#include "Window/Win32Window.h"
#include "Widgets/AssetBrowser.h"
#include <chrono>
#include "Events/EventManager.h"
#include "Engine/Input.h"
#include "ImGuizmo.h"
#include <DirectXMath.h>
#include "Math/Vector3.h"
#include "EditorApp.h"
#include "Components/Graphics/Model.h"
#include "Commands/EditorCommands.h"
#include "optick.h"
#include <winuser.h>
namespace fs = std::filesystem;

Havana::Havana(Engine* GameEngine, EditorApp* app, Moonlight::Renderer* renderer)
	: Renderer(renderer)
	, m_engine(GameEngine)
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
			Logger::GetInstance().Log(Logger::LogType::Info, "File created: " + path_to_watch);
			TestEditorEvent evt;
			evt.Path = std::move(path_to_watch);
			evt.Queue();
		}
		break;
		case FileStatus::Modified:
			Logger::GetInstance().Log(Logger::LogType::Info, "File modified: " + path_to_watch);
			break;
		case FileStatus::Deleted:
			Logger::GetInstance().Log(Logger::LogType::Info, "File deleted: " + path_to_watch);
			break;
		default:
			Logger::GetInstance().Log(Logger::LogType::Error, "Error! Unknown file status.");
		}
		});

	std::vector<TypeId> events;
	events.push_back(TestEditorEvent::GetEventId());
	events.push_back(LoadSceneEvent::GetEventId());
	events.push_back(PreviewResourceEvent::GetEventId());
	EventManager::GetInstance().RegisterReceiver(this, events);
}

void Havana::InitUI()
{
	EngineConfigFilePath = Path("Assets/Config/imgui.cfg");
	//ConfigFilePath = Path("Assets/Config/imgui.ini", true);
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
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
	colors[ImGuiCol_ModalWindowDarkening] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
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
	ImGui_ImplWin32_Init(Renderer->GetDevice().m_window);
	ImGui_ImplDX11_Init(Renderer->GetDevice().GetD3DDevice(), Renderer->GetDevice().GetD3DDeviceContext());

	Icons["Close"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Close.png"));
	Icons["BugReport"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/BugReport.png"));
	Icons["Maximize"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Maximize.png"));
	Icons["ExitMaximize"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/ExitMaximize.png"));
	Icons["Minimize"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Minimize.png"));
	Icons["Play"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Play.png"));
	Icons["Pause"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Pause.png"));
	Icons["Stop"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Stop.png"));
	Icons["Info"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Info.png"));

	m_engine->GetInput().Stop();
	GetInput().GetMouse().SetWindow(GetActiveWindow());
}

bool show_demo_window = true;
bool show_dockspace = true;
void Havana::NewFrame(std::function<void()> StartGameFunc, std::function<void()> PauseGameFunc, std::function<void()> StopGameFunc)
{
	m_input.Update();
	OPTICK_EVENT("Havana::NewFrame");
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	ImGuiIO& io = ImGui::GetIO();

	// Output size fix?
	RenderSize = Vector2(io.DisplaySize.x, io.DisplaySize.y);
	Renderer->GetDevice().SetOutputSize(RenderSize);

	DrawMainMenuBar(StartGameFunc, PauseGameFunc, StopGameFunc);
	DrawOpenFilePopup();

	static float f = 0.0f;
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	MainMenuSize.x = 0.f;
	MainMenuSize.y = 17.f;
	ImVec2 DockSize = viewport->Size;
	DockSize.y = viewport->Size.y - MainMenuSize.y;
	ImVec2 DockPos = viewport->Pos;
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

	DrawCommandPanel();
	DrawResourceMonitor();
}

void Havana::DrawOpenFilePopup()
{
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
		RecusiveDelete(child, &child->GetComponent<Transform>());
	}
	ent->MarkForDelete();
};

void Havana::DrawMainMenuBar(std::function<void()> StartGameFunc, std::function<void()> PauseGameFunc, std::function<void()> StopGameFunc)
{
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 12.f));
	if (ImGui::BeginMainMenuBar())
	{
		MainMenuSize = ImGui::GetWindowSize();
		//MainMenuSize.y = 25.f;
		ImGui::SetWindowSize(MainMenuSize);
		ImGui::PopStyleVar(1);
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
		auto Keyboard = GetInput().GetKeyboardState();
		if (!m_app->IsGameRunning())
		{
			if (ImGui::ImageButton(Icons["Play"]->ShaderResourceView, ImVec2(30.f, 30.f)) || Keyboard.F5)
			{
				ImGui::SetWindowFocus("Game");
				m_engine->GetInput().Resume();
				StartGameFunc();
			}
		}

		if (m_app->IsGameRunning())
		{
			if (ImGui::ImageButton(Icons["Pause"]->ShaderResourceView, ImVec2(30.f, 30.f)) || Keyboard.F10)
			{
				m_engine->GetInput().Pause();
				PauseGameFunc();
			}

			if (ImGui::ImageButton(Icons["Stop"]->ShaderResourceView, ImVec2(30.f, 30.f)) || (Keyboard.F5 && Keyboard.LeftShift))
			{
				SelectedTransform = nullptr;
				StopGameFunc();
				m_engine->GetInput().Stop();
			}
		}

		float endOfMenu = ImGui::GetCursorPosX();
		float buttonWidth = 40.f;
		float pos = (ImGui::GetMousePos().x - m_engine->GetWindow()->GetPosition().X());
		static_cast<Win32Window*>(m_engine->GetWindow())->CanMoveWindow((pos > endOfMenu&& pos < ImGui::GetWindowWidth() - (buttonWidth * 5.f)));

		ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);

		ImGui::SetCursorPosX((ImGui::GetWindowWidth() / 2.f) - (ImGui::CalcTextSize(WindowTitle.c_str()).x / 2.f));
		ImGui::Text(WindowTitle.c_str());

		ImGui::BeginGroup();
		ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.0f, 0.6f, 0.6f, 0.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0.f, 0.8f, 0.8f, 0.f));
		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (buttonWidth * 5.f));
		if (ImGui::ImageButton(Icons["Info"]->ShaderResourceView, ImVec2(30.f, 30.f)))
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
		}
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.35f, 126.f, 43.f, 1.f));
		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (buttonWidth * 4.f));
		if (ImGui::ImageButton(Icons["BugReport"]->ShaderResourceView, ImVec2(30.f, 30.f)))
		{
			ShellExecute(0, 0, L"https://github.com/wobbier/MitchEngine/issues", 0, 0, SW_SHOW);
		}
		ImGui::PopStyleColor(1);

		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (buttonWidth * 3.f));
		if (ImGui::ImageButton(Icons["Minimize"]->ShaderResourceView, ImVec2(30.f, 30.f)))
		{
			m_engine->GetWindow()->Minimize();
		}

		if (static_cast<Win32Window*>(m_engine->GetWindow())->IsMaximized())
		{
			ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (buttonWidth * 2.f));
			if (ImGui::ImageButton(Icons["ExitMaximize"]->ShaderResourceView, ImVec2(30.f, 30.f)))
			{
				m_engine->GetWindow()->ExitMaximize();
			}
		}
		else
		{
			ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (buttonWidth * 2.f));
			if (ImGui::ImageButton(Icons["Maximize"]->ShaderResourceView, ImVec2(30.f, 30.f)))
			{
				m_engine->GetWindow()->Maximize();
			}
		}

		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(1.f, 42.f, 43.f, 1.f));
		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth);
		//ImGui::SameLine(0.f);
		if (ImGui::ImageButton(Icons["Close"]->ShaderResourceView, ImVec2(30.f, 30.f)))
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
	static std::unordered_map<Logger::LogType, bool> DebugFilters;
	ImGuiWindowFlags window_flags = 0;
	window_flags |= ImGuiWindowFlags_MenuBar;
	bool showLog = true;
	ImGui::Begin("Log", &showLog, window_flags);
	{
		// Menu
		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("Show"))
			{
				ImGui::Checkbox("Debug", &DebugFilters[Logger::LogType::Debug]);
				ImGui::Checkbox("Error", &DebugFilters[Logger::LogType::Error]);
				ImGui::Checkbox("Info", &DebugFilters[Logger::LogType::Info]);
				ImGui::EndMenu();
			}

			if (ImGui::Button("Clear Log"))
			{
				Logger::Messages.clear();
			}

			ImGui::EndMenuBar();
		}

		bool showMessage = true;
		for (auto& msg : Logger::Messages)
		{
			if (DebugFilters.find(msg.Type) == DebugFilters.end())
			{
				DebugFilters[msg.Type] = true;
			}
			if (DebugFilters[msg.Type])
			{
				ImGui::Text(msg.Message.c_str());
			}
		}
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
	auto& kb = GetInput().GetKeyboardState();
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

	EditorCommands.Draw();
}

void Havana::DrawResourceMonitor()
{
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
		static ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollFreezeTopRow | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable;

		if (ImGui::BeginTable("##ResourceTable", 2, flags, size))
		{
			ImGui::TableSetupColumn("Resource Path", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("References", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableAutoHeaders();
			ImGuiListClipper clipper;
			clipper.Begin(resourceList.size());
			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
				{
					ImGui::TableNextRow();

					ImGui::TableSetColumnIndex(0);
					ImGui::Text(resourceList[row]->GetPath().LocalPath.c_str());

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
		if (ImGui::IsWindowFocused())
		{
			if (SelectedTransform && GetInput().GetKeyboardState().Delete)
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
		int i = 0;
		for (BaseCore* comp : world->GetAllCores())
		{
			ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | (SelectedCore == comp ? ImGuiTreeNodeFlags_Selected : 0);
			{
				node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen; // ImGuiTreeNodeFlags_Bullet
				ImGui::TreeNodeEx((void*)(intptr_t)i, node_flags, comp->GetName().c_str());
				if (ImGui::IsItemClicked())
				{
					SelectedCore = comp;
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
		SelectedCore->OnEditorInspect();
	}
	ImGui::End();

	ImGui::Begin("Preview");
	{
		if (ViewTexture)
		{
			if (ViewTexture->ShaderResourceView)
			{
				// Get the current cursor position (where your window is)
				ImVec2 pos = ImGui::GetCursorScreenPos();
				ImVec2 maxPos = ImVec2(pos.x + ImGui::GetWindowSize().x, pos.y + ImGui::GetWindowSize().y);
				Vector2 RenderSize = Vector2(ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);

				// Ask ImGui to draw it as an image:
				// Under OpenGL the ImGUI image type is GLuint
				// So make sure to use "(void *)tex" but not "&tex"
				ImGui::GetWindowDrawList()->AddImage(
					(void*)ViewTexture->ShaderResourceView,
					ImVec2(pos.x, pos.y),
					ImVec2(maxPos));
				//ImVec2(WorldViewRenderSize.X() / RenderSize.X(), WorldViewRenderSize.Y() / RenderSize.Y()));

			}
		}
	}
	ImGui::End();
}

void Havana::UpdateWorldRecursive(Transform* root)
{
	if (!root)
		return;
	if (ImGui::BeginDragDropTarget())
	{
		HandleAssetDragAndDrop(root);

		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_CHILD_TRANSFORM"))
		{
			DragParentDescriptor.Parent->SetParent(root->Parent);
		}
		ImGui::EndDragDropTarget();
	}

	int i = 0;
	for (EntityHandle& child : root->GetChildren())
	{
		Transform* var = &child->GetComponent<Transform>();
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
					DragParentDescriptor.Parent->SetParent(child);
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
					DragParentDescriptor.Parent->SetParent(child);
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
			ent->AddComponent<Transform>(payload_n.Name.substr(0, payload_n.Name.find_last_of('.'))).SetParent(root->Parent);
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
			ent->AddComponent<Transform>().SetParent(transform->Parent);
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
		m_engine->GetInput().SetMouseOffset(GameViewRenderLocation);
		if (GetInput().GetKeyboardState().Escape/*  && m_app->IsGameRunning()&& AllowGameInput*/)
		{
			m_engine->GetInput().Stop();
			AllowGameInput = false;
			ImGui::SetWindowFocus("World View");
		}
		else if(ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)/* && AllowGameInput*/)
		{
			m_engine->GetInput().Resume();
			AllowGameInput = true;
		}
		//AllowGameInput = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
		if (Renderer->GameViewRTT)
		{
			static auto srv = Renderer->GameViewRTT->ShaderResourceView;
			static std::string RenderTextureName = "Shaded";
			if (ImGui::BeginMenuBar())
			{
				if (ImGui::BeginMenu(RenderTextureName.c_str()))
				{
					if (ImGui::MenuItem("Shaded", "", false))
					{
						RenderTextureName = "Shaded";

					}
					if (ImGui::MenuItem("Diffuse", "", false))
					{
						RenderTextureName = "Diffuse";
					}
					if (ImGui::MenuItem("Normals", "", false))
					{
						RenderTextureName = "Normals";
					}
					if (ImGui::MenuItem("Specular", "", false))
					{
						RenderTextureName = "Specular";
					}
					if (ImGui::MenuItem("Depth", "", false))
					{
						RenderTextureName = "Depth";
					}
					if (ImGui::MenuItem("UI", "", false))
					{
						RenderTextureName = "UI";
					}
					if (ImGui::MenuItem("Position", "", false))
					{
						RenderTextureName = "Position";
					}
					if (ImGui::MenuItem("Shadow", "", false))
					{
						RenderTextureName = "Shadow";
					}
					ImGui::EndMenu();
				}

				ImGui::EndMenuBar();
			}
			if (RenderTextureName == "Shaded")
			{
				srv = Renderer->GameViewRTT->ShaderResourceView;
			}
			else if (RenderTextureName == "Diffuse")
			{
				srv = Renderer->GameViewRTT->ColorShaderResourceView;
			}
			else if (RenderTextureName == "Normals")
			{
				srv = Renderer->GameViewRTT->NormalShaderResourceView;
			}
			else if (RenderTextureName == "Specular")
			{
				srv = Renderer->GameViewRTT->SpecularShaderResourceView;
			}
			else if (RenderTextureName == "Depth")
			{
				srv = Renderer->GameViewRTT->DepthShaderResourceView;
			}
			else if (RenderTextureName == "UI")
			{
				srv = Renderer->GameViewRTT->UIShaderResourceView;
			}
			else if (RenderTextureName == "Position")
			{
				srv = Renderer->GameViewRTT->PositionShaderResourceView;
			}
			else if (RenderTextureName == "Shadow")
			{
				srv = Renderer->GameViewRTT->ShadowMapShaderResourceView;
			}
			m_isGameFocused = ImGui::IsWindowFocused();

			if (Renderer->GameViewRTT && srv != nullptr)
			{
				// Get the current cursor position (where your window is)
				ImVec2 pos = ImGui::GetCursorScreenPos();
				ImVec2 maxPos = ImVec2(pos.x + ImGui::GetWindowSize().x, pos.y + ImGui::GetWindowSize().y);
				//GameRenderSize = Vector2(ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);
				GameRenderSize = Vector2(ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);
				GameViewRenderLocation = Vector2(pos.x, pos.y);
				
				// Ask ImGui to draw it as an image:
				// Under OpenGL the ImGUI image type is GLuint
				// So make sure to use "(void *)tex" but not "&tex"
				ImGui::GetWindowDrawList()->AddImage(
					(void*)srv.Get(),
					ImVec2(pos.x, pos.y),
					ImVec2(maxPos),
					ImVec2(0, 0),
					ImVec2(Mathf::Clamp(0.f, 1.0f, GameRenderSize.X() / Renderer->GameViewRTT->Width), Mathf::Clamp(0.f, 1.0f, GameRenderSize.Y() / Renderer->GameViewRTT->Height)));
				//ImVec2(WorldViewRenderSize.X() / RenderSize.X(), WorldViewRenderSize.Y() / RenderSize.Y()));

			}
		}
		ImGui::End();

		bool showWorldEditorWindow = true;
		ImGui::Begin("World View", &showWorldEditorWindow, window_flags);

		//if (!EditorCamera.OutputSize.IsZero())
		if (Renderer->SceneViewRTT)
		{
			static auto srv = Renderer->SceneViewRTT->ShaderResourceView;
			static std::string RenderTextureName = "Diffuse";
			if (ImGui::BeginMenuBar())
			{
				if (ImGui::BeginMenu(RenderTextureName.c_str()))
				{
					if (ImGui::MenuItem("Shaded", "", false))
					{
						RenderTextureName = "Shaded";

					}
					if (ImGui::MenuItem("Diffuse", "", false))
					{
						RenderTextureName = "Diffuse";
					}
					if (ImGui::MenuItem("Normals", "", false))
					{
						RenderTextureName = "Normals";
					}
					if (ImGui::MenuItem("Specular", "", false))
					{
						RenderTextureName = "Specular";
					}
					if (ImGui::MenuItem("Position", "", false))
					{
						RenderTextureName = "Position";
					}
					if (ImGui::MenuItem("Depth", "", false))
					{
						RenderTextureName = "Depth";
					}
					if (ImGui::MenuItem("Shadow", "", false))
					{
						RenderTextureName = "Shadow";
					}
					if (ImGui::MenuItem("Pick Mask", "", false))
					{
						RenderTextureName = "Pick Mask";
					}
					ImGui::EndMenu();
				}

				ImGui::EndMenuBar();
			}
			if (RenderTextureName == "Shaded")
			{
				srv = Renderer->SceneViewRTT->ShaderResourceView;
			}
			else if (RenderTextureName == "Diffuse")
			{
				srv = Renderer->SceneViewRTT->ColorShaderResourceView;
			}
			else if (RenderTextureName == "Normals")
			{
				srv = Renderer->SceneViewRTT->NormalShaderResourceView;
			}
			else if (RenderTextureName == "Specular")
			{
				srv = Renderer->SceneViewRTT->SpecularShaderResourceView;
			}
			else if (RenderTextureName == "Position")
			{
				srv = Renderer->SceneViewRTT->PositionShaderResourceView;
			}
			else if (RenderTextureName == "Depth")
			{
				srv = Renderer->SceneViewRTT->DepthShaderResourceView;
			}
			else if (RenderTextureName == "Shadow")
			{
				srv = Renderer->SceneViewRTT->ShadowMapShaderResourceView;
			}
			else if (RenderTextureName == "Pick Mask")
			{
				srv = Renderer->SceneViewRTT->PickingResourceView;
			}
			// Get the current cursor position (where your window is)
			ImVec2 pos = ImGui::GetCursorScreenPos();
			Vector2 evt;
			evt.SetX((GetEngine().GetWindow()->GetPosition().X() + GetInput().GetMousePosition().X()) - pos.x);
			evt.SetY((GetEngine().GetWindow()->GetPosition().Y() + GetInput().GetMousePosition().Y()) - pos.y);
			mouseTracker.Update(GetInput().GetMouseState());
			static bool hasClicked = false;
			if (Renderer && GetInput().GetMouseState().leftButton && !hasClicked)
			{
				Renderer->PickScene(evt);
				hasClicked = true;
			}
			else
			{
				hasClicked = false;
			}

			DirectX::XMFLOAT4X4 objView;
			if (SelectedTransform)
			{
				DirectX::XMStoreFloat4x4(&objView, SelectedTransform->GetMatrix().GetInternalMatrix());
			}

			static ImGuizmo::OPERATION mCurrentGizmoOperation(ImGuizmo::TRANSLATE);
			static ImGuizmo::MODE mCurrentGizmoMode(ImGuizmo::LOCAL);
			static bool useSnap = false;
			static float snap[3] = { 1.f, 1.f, 1.f };

			float matrixTranslation[3], matrixRotation[3], matrixScale[3];
			//ImGuizmo::DecomposeMatrixToComponents(&objView._11, matrixTranslation, matrixRotation, matrixScale);

			bool isMovingMouse = (ImGui::GetMousePos().x != previousMousePos.x) && (ImGui::GetMousePos().y != previousMousePos.y);
			previousMousePos = ImGui::GetMousePos();
			if (ImGui::IsWindowFocused() && ImGuizmo::IsUsing() && isMovingMouse) {
				//ImGui::InputFloat3("Tr", matrixTranslation, 3);
				if (SelectedTransform)
				{
					//HavanaUtils::EditableVector3("RtVec", SelectedTransform->Rotation);
					//matrixRotation[0] = SelectedTransform->Rotation[0] * DirectX::XM_PI / 180.f;
					//matrixRotation[1] = SelectedTransform->Rotation[1] * DirectX::XM_PI / 180.f;
					//matrixRotation[2] = SelectedTransform->Rotation[2] * DirectX::XM_PI / 180.f;
				}
				//else
				//{
				//	ImGui::InputFloat3("Rt", matrixRotation, 3);
				//}
				//ImGui::InputFloat3("Sc", matrixScale, 3);
				//ImGuizmo::RecomposeMatrixFromComponents(matrixTranslation, matrixRotation, matrixScale, &objView._11);
			}

			{
				m_isWorldViewFocused = ImGui::IsWindowFocused();


				if (srv != nullptr)
				{
					ImVec2 maxPos = ImVec2(pos.x + ImGui::GetWindowSize().x, pos.y + ImGui::GetWindowSize().y);
					WorldViewRenderSize = Vector2(ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);
					WorldViewRenderLocation = Vector2(pos.x, pos.y);
					// Ask ImGui to draw it as an image:
					// Under OpenGL the ImGUI image type is GLuint
					// So make sure to use "(void *)tex" but not "&tex"
					ImGui::GetWindowDrawList()->AddImage(
						(void*)srv.Get(),
						ImVec2(pos.x, pos.y),
						ImVec2(maxPos),
						ImVec2(0, 0),
						ImVec2(Mathf::Clamp(0.f, 1.0f, WorldViewRenderSize.X() / Renderer->SceneViewRTT->Width), Mathf::Clamp(0.f, 1.0f, WorldViewRenderSize.Y() / Renderer->SceneViewRTT->Height)));

					ImGuizmo::SetRect(WorldViewRenderLocation.X(), WorldViewRenderLocation.Y(), WorldViewRenderSize.X(), WorldViewRenderSize.Y());

					DirectX::XMFLOAT4X4 fView;
					if (EditorCamera.Projection == Moonlight::ProjectionType::Perspective)
					{
						DirectX::XMMATRIX perspectiveMatrix = DirectX::XMMatrixPerspectiveFovRH(
							EditorCamera.FOV * DirectX::XM_PI / 180.0f,
							WorldViewRenderSize.X() / WorldViewRenderSize.Y(),
							.1f,
							1000.0f
						);

						DirectX::XMStoreFloat4x4(&fView, perspectiveMatrix);
					}
					else
					{
						DirectX::XMMATRIX perspectiveMatrix = DirectX::XMMatrixOrthographicRH(
							EditorCamera.OutputSize.X() / EditorCamera.OrthographicSize,
							EditorCamera.OutputSize.Y() / EditorCamera.OrthographicSize,
							.1f,
							100.0f
						);

						DirectX::XMStoreFloat4x4(&fView, perspectiveMatrix);
					}
					ImGuizmo::SetDrawlist();
					ImGuizmo::SetOrthographic(false);

					const DirectX::XMVECTORF32 eye = { EditorCamera.Position.X(), EditorCamera.Position.Y(), EditorCamera.Position.Z(), 0 };
					const DirectX::XMVECTORF32 at = { EditorCamera.Position.X() + EditorCamera.Front.X(), EditorCamera.Position.Y() + EditorCamera.Front.Y(), EditorCamera.Position.Z() + EditorCamera.Front.Z(), 0.0f };
					const DirectX::XMVECTORF32 up = { Vector3::Up.X(), Vector3::Up.Y(), Vector3::Up.Z(), 0 };

					DirectX::XMMATRIX vec = DirectX::XMMatrixLookAtRH(eye, at, up);

					DirectX::XMFLOAT4X4 fView2;
					DirectX::XMStoreFloat4x4(&fView2, vec);

					DirectX::XMFLOAT4X4 idView;
					DirectX::XMStoreFloat4x4(&idView, DirectX::XMMatrixIdentity());

					//ImGuizmo::DrawGrid(&fView2._11, &fView._11, &idView._11, 10.f);
					//ImGuizmo::DrawCube(&fView2._11, &fView._11, &idView._11);
					ImGuizmo::Manipulate(&fView2._11, &fView._11, mCurrentGizmoOperation, mCurrentGizmoMode, &objView._11, &idView._11, useSnap ? &snap[0] : NULL);
					if (ImGui::IsWindowFocused() && ImGuizmo::IsUsing()/* && isMovingMouse*/)
					{
						if (SelectedTransform)
						{
							ImGuizmo::DecomposeMatrixToComponents(&objView._11, matrixTranslation, matrixRotation, matrixScale);
							if (matrixRotation[0] != prevMatrixRotation[0])
							{
								BRUH("Something is off");
							}
							//memcpy(matrixTranslation, prevMatrixTranslation, sizeof(float) * 3);
							//memcpy(matrixRotation, prevMatrixRotation, sizeof(float) * 3);
							prevMatrixRotation[0] = matrixRotation[0];
							prevMatrixRotation[1] = matrixRotation[1];
							prevMatrixRotation[2] = matrixRotation[2];
							//memcpy(matrixScale, prevMatrixScale, sizeof(float) * 3);

							SelectedTransform->SetPosition(Vector3(matrixTranslation[0], matrixTranslation[1], matrixTranslation[2]));
							SelectedTransform->SetRotation(Vector3(matrixRotation[0], matrixRotation[1], matrixRotation[2]));
							//SelectedTransform->SetRotation(Vector3(matrixRotation[0] * 180.f / DirectX::XM_PI, matrixRotation[1] * 180.f / DirectX::XM_PI, matrixRotation[2] * 180.f / DirectX::XM_PI));
						}
					}
				}
			}

			if (ImGui::IsKeyPressed(90))
				mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
			if (ImGui::IsKeyPressed(69))
				mCurrentGizmoOperation = ImGuizmo::ROTATE;
			if (ImGui::IsKeyPressed(82)) // r Key
				mCurrentGizmoOperation = ImGuizmo::SCALE;
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
		}
		ImGui::End();
		ImGui::PopStyleVar(3);
	}

	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	// Update and Render additional Platform Windows
	if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
	}
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