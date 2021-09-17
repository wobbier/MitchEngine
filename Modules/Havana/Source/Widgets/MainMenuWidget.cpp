#include "MainMenuWidget.h"
#include <optick.h>
#include <Graphics/Texture.h>
#include <Resource/ResourceCache.h>
#include <Utils/ImGuiUtils.h>
#include <Events/SceneEvents.h>
#include <Engine/Engine.h>
#include <Commands/EditorCommands.h>
#include <Window/EditorWindow.h>
#include <EditorApp.h>
#include <Havana.h>
#include <Utils/StringUtils.h>
#include <Utils/PlatformUtils.h>
#include <HavanaEvents.h>

#if ME_EDITOR

MainMenuWidget::MainMenuWidget(Havana* editorApp)
	: HavanaWidget("Main Menu")
	, Editor(editorApp)
{
}

void MainMenuWidget::Init()
{
	Icons["Close"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Close.png"));
	Icons["Maximize"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Maximize.png"));
	Icons["ExitMaximize"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/ExitMaximize.png"));
	Icons["Minimize"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Minimize.png"));

	Icons["Play"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Play.png"));
	Icons["Pause"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Pause.png"));
	Icons["Stop"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Stop.png"));

	Icons["BugReport"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/BugReport.png"));
	Icons["Info"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Info.png"));
	Icons["Logo"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/ME-LOGO.png"));
	Icons["Profiler"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Profiler.png"));

	CurrentDirectory = Path("Assets");
}

void MainMenuWidget::Destroy()
{
	Icons.clear();
}

void MainMenuWidget::SetData(std::vector<SharedPtr<HavanaWidget>>* widgetList, EditorApp* editorApp)
{
	WidgetList = widgetList;
	App = editorApp;
}

void MainMenuWidget::SetWindowTitle(const std::string& title)
{
	WindowTitle = title;
}

void MainMenuWidget::Update()
{

}

void MainMenuWidget::Render()
{
	bool RequestLoadScene = false;
	auto& input = Editor->GetInput();
	if ((input.IsKeyDown(KeyCode::LeftControl) || input.IsKeyDown(KeyCode::RightControl))
		&& input.WasKeyPressed(KeyCode::O))
	{
		RequestLoadScene = true;
	}
	OPTICK_CATEGORY("Main Menu Bar", Optick::Category::Debug);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 12.f));
	if (ImGui::BeginMainMenuBar())
	{
		Input& editorInput = GetEngine().GetEditorInput();
		Input& gameInput = GetEngine().GetInput();

		MainMenuSize = ImGui::GetWindowSize();
		//MainMenuSize.y = 25.f;
		ImGui::SetWindowSize(MainMenuSize);
		ImGui::PopStyleVar(1);
		ImGui::Image(Icons["Logo"]->TexHandle, ImVec2(35, 35));
		if (ImGui::BeginMenu("File"))
		{
			//ImGui::MenuItem("(dummy menu)", NULL, false, false);
			if (ImGui::MenuItem("New Scene"))
			{

#if ME_PLATFORM_WIN64
				NewSceneEvent evt;
				evt.Queue();
#endif
			}
			if (ImGui::MenuItem("Open Scene", "Ctrl+O"))
			{
				RequestLoadScene = true;
			}

			if (ImGui::MenuItem("Save", "Ctrl+S"))
			{
				if (GetEngine().CurrentScene/* && !std::filesystem::exists(m_engine->CurrentScene->Path.FullPath)*/)
				{
					SaveSceneEvent evt;
					//ImGui::OpenPopup("help_popup");
					evt.Fire();
				}

			}
			//if (ImGui::MenuItem("Save As..")) {}
			//ImGui::Separator();

			if (ImGui::MenuItem("Quit", "Alt+F4"))
			{
				GetEngine().Quit();
			}
			ImGui::EndMenu();
		}
		//if (ImGui::BeginMenu("Edit"))
		//{
		//	const bool canUndo = EditorCommands.CanUndo();

		//	if (ImGui::MenuItem("Undo", "CTRL+Z", false, canUndo))
		//	{
		//		EditorCommands.Undo();
		//	}

		//	const bool canRedo = EditorCommands.CanRedo();
		//	if (ImGui::MenuItem("Redo", "CTRL+Y", false, canRedo))
		//	{
		//		EditorCommands.Redo();
		//	}

		//	/*ImGui::Separator();
		//	if (ImGui::MenuItem("Cut", "CTRL+X")) {}
		//	if (ImGui::MenuItem("Copy", "CTRL+C")) {}
		//	if (ImGui::MenuItem("Paste", "CTRL+V")) {}*/
		//	ImGui::EndMenu();
		//}
		//if (ImGui::BeginMenu("Add"))
		//{
		//	if (ImGui::MenuItem("Entity"))
		//	{
		//		// The fuck is this garbage
		//		CreateEntity* cmd = new CreateEntity();
		//		EditorCommands.Push(cmd);

		//		AddComponentCommand* compCmd = new AddComponentCommand("Transform", cmd->Ent);
		//		EditorCommands.Push(compCmd);

		//		Transform* transform = static_cast<Transform*>(compCmd->GetComponent());
		//		transform->SetName("New Entity");
		//	}
		//	ImGui::EndMenu();
		//}

		if (ImGui::BeginMenu("View"))
		{
			if (WidgetList)
			{
				std::vector<SharedPtr<HavanaWidget>>& list = *WidgetList;
				for (int i = 0; i < list.size(); ++i)
				{
					if (list[i].get() != this && ImGui::MenuItem(list[i]->Name.c_str(), list[i]->Hotkey.c_str(), &list[i]->IsOpen))
					{
					}
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Help"))
		{
			if (ImGui::MenuItem("Show ImGui Demo"))
			{
				ShowDemoWindow = !ShowDemoWindow;
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
		if (!App->IsGameRunning())
		{
			if (ImGui::ImageButton(Icons["Play"]->TexHandle, ImVec2(30.f, 30.f)) || editorInput.IsKeyDown(KeyCode::F5))
			{
				ImGui::SetWindowFocus("Game");
				gameInput.Resume();
				StartGameFunc();
			}
		}

		if (App->IsGameRunning())
		{
			if (ImGui::ImageButton(Icons["Pause"]->TexHandle, ImVec2(30.f, 30.f)) || editorInput.IsKeyDown(KeyCode::F10))
			{
				gameInput.Pause();
				PauseGameFunc();
			}

			if (ImGui::ImageButton(Icons["Stop"]->TexHandle, ImVec2(30.f, 30.f)) || (editorInput.IsKeyDown(KeyCode::F5) && editorInput.IsKeyDown(KeyCode::LeftShift)))
			{
				MaximizeOnPlay = false;
				ClearInspectEvent evt;
				evt.Fire();
				StopGameFunc();
				gameInput.Stop();
			}
		}

		float endOfMenu = ImGui::GetCursorPosX();
		float buttonWidth = 40.f;
		TitleBarDragPosition = Vector2(endOfMenu, 10.f);
		float winWidth = ImGui::GetWindowWidth();
		TitleBarDragSize = Vector2(winWidth - endOfMenu - (buttonWidth * 5.f), MainMenuSize.y - 10.f);

#if ME_PLATFORM_WIN64
		auto window = static_cast<EditorWindow*>(GetEngine().GetWindow());

		window->SetDragBounds(TitleBarDragPosition, TitleBarDragSize);
#endif
		Vector2 pos = editorInput.GetMousePosition();
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
		//ImGui::Text("%.1f fps", (float)ImGui::GetIO().Framerate);

		ImGui::SetCursorPosX((ImGui::GetWindowWidth() / 2.f) - (ImGui::CalcTextSize(WindowTitle.c_str()).x / 2.f));
		ImGui::Text(WindowTitle.c_str());

		ImGui::BeginGroup();
		ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.0f, 0.6f, 0.6f, 0.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0.f, 0.8f, 0.8f, 0.f));
		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (buttonWidth * 5.f));
		if (ImGui::ImageButton(Icons["Profiler"]->TexHandle, ImVec2(30.f, 30.f)))
		{
			Path optickPath = Path("Engine/Tools/Optick.exe");
			// additional information
			PlatformUtils::RunProcess(optickPath);
		}
		//if (ImGui::ImageButton(Icons["Info"]->TexHandle, ImVec2(30.f, 30.f)))
		//{
		//	if (m_engine->CurrentScene && !std::filesystem::exists(m_engine->CurrentScene->FilePath.FullPath))
		//	{
		//		ImGui::OpenPopup("help_popup");
		//	}
		//}

		//if (ImGui::BeginPopup("help_popup"))
		//{
		//	ImGui::Text("Components");
		//	ImGui::Separator();

		//	ComponentRegistry& reg = GetComponentRegistry();

		//	for (auto& thing : reg)
		//	{
		//		if (ImGui::Selectable(thing.first.c_str()))
		//		{
		//			if (SelectedEntity)
		//			{
		//				AddComponentCommand* compCmd = new AddComponentCommand(thing.first, SelectedEntity);
		//				EditorCommands.Push(compCmd);
		//			}
		//			if (SelectedTransform)
		//			{
		//				AddComponentCommand* compCmd = new AddComponentCommand(thing.first, SelectedTransform->Parent);
		//				EditorCommands.Push(compCmd);
		//			}
		//		}
		//	}
		//	ImGui::EndPopup();
		//}
		float RightShift = 2.f;
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.35f, 126.f, 43.f, 1.f));
		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (buttonWidth * 4.f));
		if (ImGui::ImageButton(Icons["BugReport"]->TexHandle, ImVec2(30.f, 30.f)))
		{
#if ME_PLATFORM_WIN64
			ShellExecute(0, 0, L"https://github.com/wobbier/MitchEngine/issues", 0, 0, SW_SHOW);
#endif
		}
		ImGui::PopStyleColor(1);

		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (buttonWidth * 3.f) + RightShift);
		if (ImGui::ImageButton(Icons["Minimize"]->TexHandle, ImVec2(30.f, 30.f)))
		{
			GetEngine().GetWindow()->Minimize();
		}

#if ME_PLATFORM_WIN64
		if (static_cast<EditorWindow*>(GetEngine().GetWindow())->IsMaximized())
		{
			ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (buttonWidth * 2.f) + RightShift);
			if (ImGui::ImageButton(Icons["ExitMaximize"]->TexHandle, ImVec2(30.f, 30.f)))
			{
				GetEngine().GetWindow()->ExitMaximize();
			}
		}
		else
#endif
		{
			ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (buttonWidth * 2.f) + RightShift);
			if (ImGui::ImageButton(Icons["Maximize"]->TexHandle, ImVec2(30.f, 30.f)))
			{
				GetEngine().GetWindow()->Maximize();
			}
		}

		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(1.f, 42.f, 43.f, 1.f));
		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth + RightShift);
		//ImGui::SameLine(0.f);
		if (ImGui::ImageButton(Icons["Close"]->TexHandle, ImVec2(30.f, 30.f)))
		{
			GetEngine().GetWindow()->Exit();
		}
		ImGui::PopStyleColor(3);
		ImGui::EndGroup();

		ImGui::EndMainMenuBar();
	}

	if (ShowDemoWindow)
	{
		ImGui::ShowDemoWindow(&ShowDemoWindow);
	}

	if (RequestLoadScene)
	{
		RequestAssetSelectionEvent evt([this](Path selectedAsset) {
			LoadSceneEvent evt;
			evt.Level = selectedAsset.LocalPath;
			evt.Fire();

			GetEngine().GetConfig().SetValue(std::string("CurrentScene"), GetEngine().CurrentScene->FilePath.LocalPath);
			}, AssetType::Level);
		evt.Fire();
	}
}

void MainMenuWidget::SetCallbacks(std::function<void()> StartGame, std::function<void()> PauseGame, std::function<void()> StopGame)
{
	StartGameFunc = StartGame;
	PauseGameFunc = PauseGame;
	StopGameFunc = StopGame;
}

#endif