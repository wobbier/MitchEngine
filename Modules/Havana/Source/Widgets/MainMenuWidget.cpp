#include "MainMenuWidget.h"
#include <optick.h>
#include <Graphics/Texture.h>
#include <Resource/ResourceCache.h>
#include <Utils/ImGuiUtils.h>
#include <Events/SceneEvents.h>
#include <Engine/Engine.h>
#include <Commands/EditorCommands.h>
#include <EditorApp.h>
#include <Havana.h>
#include <Utils/StringUtils.h>
#include <Utils/PlatformUtils.h>
#include <Events/HavanaEvents.h>
#include <Window/SDLWindow.h>
#include "UI/Colors.h"

#if USING( ME_EDITOR )

MainMenuWidget::MainMenuWidget( Havana* editorApp )
    : HavanaWidget( "Main Menu" )
    , Editor( editorApp )
{
}

void MainMenuWidget::Init()
{
    Icons["Close"] = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Havana/UI/Close.png" ) );
    Icons["Maximize"] = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Havana/UI/Maximize.png" ) );
    Icons["ExitMaximize"] = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Havana/UI/ExitMaximize.png" ) );
    Icons["Minimize"] = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Havana/UI/Minimize.png" ) );

    Icons["Play"] = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Havana/UI/Play.png" ) );
    Icons["Pause"] = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Havana/UI/Pause.png" ) );
    Icons["Stop"] = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Havana/UI/Stop.png" ) );

    Icons["BugReport"] = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Havana/UI/BugReport.png" ) );
    Icons["Info"] = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Havana/UI/Info.png" ) );
    Icons["Logo"] = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Havana/ME-LOGO.png" ) );
    Icons["Profiler"] = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Havana/UI/Profiler.png" ) );


    auto cb = [this]( const Vector2& pos ) -> std::optional<SDL_HitTestResult>
    {
        if( pos > TitleBarDragPosition && pos < TitleBarDragPosition + TitleBarDragSize )
        {
            return SDL_HitTestResult::SDL_HITTEST_DRAGGABLE;
        }

        return std::nullopt;
    };
    auto window = static_cast<SDLWindow*>( GetEngine().GetWindow() );
    window->SetBorderless( true );
    window->SetCustomDragCallback( cb );
    //Vector2 pos = Editor->GetInput().GetMousePosition();
    //SDL_SetWindowGrab(window->WindowHandle, (pos > TitleBarDragPosition && pos < TitleBarDragPosition + TitleBarDragSize) ? SDL_TRUE : SDL_FALSE);
}

void MainMenuWidget::Destroy()
{
    Icons.clear();
}

void MainMenuWidget::SetData( std::vector<SharedPtr<HavanaWidget>>* widgetList, EditorApp* editorApp )
{
    WidgetList = widgetList;
    App = editorApp;
}

void MainMenuWidget::SetWindowTitle( const std::string& title )
{
    WindowTitle = title;
}

void MainMenuWidget::Update()
{
    //auto window = static_cast<SDLWindow*>(GetEngine().GetWindow());
    //window->SetBorderless(true);
    //window->SetCustomDragCallback(cb);
    //Vector2 pos = Editor->GetInput().GetMousePosition();
    //SDL_SetWindowGrab(window->WindowHandle, /*(pos > TitleBarDragPosition && pos < TitleBarDragPosition + TitleBarDragSize) ?*/ SDL_TRUE /*: SDL_FALSE*/);
}

void MainMenuWidget::Render()
{
    OPTICK_EVENT( "MainMenuWidget::Render", Optick::Category::UI );
    bool RequestLoadScene = false;
    bool RequestSaveScene = false;
    bool RequestSaveAsScene = false;

    auto& input = Editor->GetInput();
    if( ( input.IsKeyDown( KeyCode::LeftControl ) || input.IsKeyDown( KeyCode::RightControl ) )
        && input.WasKeyPressed( KeyCode::O ) )
    {
        RequestLoadScene = true;
    }

    // Saving hotkeys
    if( ( input.IsKeyDown( KeyCode::LeftControl ) && input.IsKeyDown( KeyCode::LeftShift ) )
        && input.WasKeyPressed( KeyCode::S ) )
    {
        RequestSaveAsScene = true;
    }
    else if( input.IsKeyDown( KeyCode::LeftControl )
        && input.WasKeyPressed( KeyCode::S ) )
    {
        RequestSaveScene = true;
    }

    ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 0.0f, 12.f ) );
    ImGui::PushStyleColor( ImGuiCol_MenuBarBg, COLOR_BACKGROUND_BORDER );
    if( ImGui::BeginMainMenuBar() )
    {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar( 1 );

        Input& editorInput = GetEngine().GetEditorInput();
        Input& gameInput = GetEngine().GetInput();

        MainMenuSize = ImGui::GetWindowSize();
        //MainMenuSize.y = 25.f;
        //ImGui::SetWindowSize(MainMenuSize);
        ImGui::Image( Icons["Logo"]->TexHandle, ImVec2( 35, 35 ) );
        if( ImGui::BeginMenu( "File" ) )
        {
            if( ImGui::MenuItem( "New Scene" ) )
            {

#if USING( ME_PLATFORM_WIN64 )
                NewSceneEvent evt;
                evt.Queue();
#endif
            }
            if( ImGui::MenuItem( "Open Scene", "Ctrl+O" ) )
            {
                RequestLoadScene = true;
            }

            if( ImGui::MenuItem( "Save", "Ctrl+S" ) )
            {
                if( GetEngine().CurrentScene )
                {
                    SaveSceneEvent evt;
                    evt.Fire();
                }

            }
            if( ImGui::MenuItem( "Save As..", "Ctrl+Shift+S" ) )
            {
                SaveSceneEvent evt;
                evt.SaveAs = true;
                evt.Fire();
            }
            ImGui::Separator();

            if( ImGui::MenuItem( "Quit", "Alt+F4" ) )
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

        if( ImGui::BeginMenu( "View" ) )
        {
            if( WidgetList )
            {
                std::vector<SharedPtr<HavanaWidget>>& list = *WidgetList;
                for( auto& i : list )
                {
                    if( i.get() != this && ImGui::MenuItem( i->Name.c_str(), i->Hotkey.c_str(), &i->IsOpen ) )
                    {
                    }
                }
            }
            ImGui::EndMenu();
        }

        if( ImGui::BeginMenu( "Help" ) )
        {
            if( ImGui::MenuItem( "Show ImGui Demo" ) )
            {
                ShowDemoWindow = !ShowDemoWindow;
            }
            ImGui::Separator();
            if( ImGui::MenuItem( "About" ) )
            {
                ShowAboutWindow = true;
            }
            ImGui::EndMenu();
        }

        if( !App->IsGameRunning() )
        {
            if( ImGui::ImageButton( Icons["Play"]->TexHandle, ImVec2( 30.f, 30.f ) ) || editorInput.IsKeyDown( KeyCode::F5 ) )
            {
                ImGui::SetWindowFocus( "Game" );
                StartGameFunc();
                gameInput.Resume();
                Editor->GetInput().Stop();
            }
        }

        if( App->IsGameRunning() )
        {
            if( ImGui::ImageButton( Icons["Pause"]->TexHandle, ImVec2( 30.f, 30.f ) ) || editorInput.IsKeyDown( KeyCode::F10 ) )
            {
                gameInput.Pause();
                PauseGameFunc();
            }

            if( ImGui::ImageButton( Icons["Stop"]->TexHandle, ImVec2( 30.f, 30.f ) ) || ( editorInput.IsKeyDown( KeyCode::F5 ) && editorInput.IsKeyDown( KeyCode::LeftShift ) ) )
            {
                MaximizeOnPlay = false;
                ClearInspectEvent evt;
                evt.Fire();
                StopGameFunc();
                gameInput.Stop();
                Editor->GetInput().Resume();
            }
        }

        const float endOfMenu = ImGui::GetCursorPosX();
        const float buttonWidth = 40.f;
        TitleBarDragPosition = Vector2( endOfMenu, 10.f );
        const float winWidth = ImGui::GetWindowWidth();
        TitleBarDragSize = Vector2( winWidth - endOfMenu - ( buttonWidth * 5.f ), MainMenuSize.y - 10.f );

        // Window Maximize
        {
            const Vector2 pos = editorInput.GetMousePosition();

            if( ImGui::IsMouseDoubleClicked( 0 ) && ( pos > TitleBarDragPosition && pos < TitleBarDragPosition + TitleBarDragSize ) )
            {
                GetEngine().GetWindow()->Maximize();
            }
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
        {
            OPTICK_EVENT( "FPS", Optick::Category::UI );

            // increase the counter by one
            static int m_fpscount = 0;
            static int fps = 0;
            m_fpscount++;
            ++frameCount;

            static float fpsTime = 0;
            fpsTime += GetEngine().DeltaTime;
            // one second elapsed? (= 1000 milliseconds)
            if( fpsTime >= 1.f )
            {
                frametime = GetEngine().DeltaTime;
                frameCount = 0;

                // save the current counter value to m_fps
                fps = m_fpscount;

                // reset the counter and the interval
                m_fpscount = 0;
                fpsTime -= 1.f;
            }

            ImGui::Text( "%.1f ms", frametime * 1000.f );
            ImGui::Text( "%.1f fps", static_cast<float>( fps ) );
            //ImGui::Text("%.1f fps", (float)ImGui::GetIO().Framerate);
        }

        ImGui::SetCursorPosX( ( ImGui::GetWindowWidth() / 2.f ) - ( ImGui::CalcTextSize( WindowTitle.c_str() ).x / 2.f ) );
        ImGui::Text( WindowTitle.c_str() );

        ImGui::BeginGroup();
        ImGui::PushStyleColor( ImGuiCol_Button, static_cast<ImVec4>( ImColor::HSV( 0.0f, 0.6f, 0.6f, 0.f ) ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonActive, static_cast<ImVec4>( ImColor::HSV( 0.f, 0.8f, 0.8f, 0.f ) ) );
        ImGui::SetCursorPosX( ImGui::GetWindowWidth() - ( buttonWidth * 4.25f ) );
        if( ImGui::ImageButton( Icons["Profiler"]->TexHandle, ImVec2( 30.f, 30.f ) ) )
        {
            const Path optickPath = Path( "Engine/Tools/Optick.exe" );
            // additional information
            PlatformUtils::RunProcess( optickPath );
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
        const float RightShift = 2.f;
        //		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ACCENT_GREEN);
        //		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (buttonWidth * 4.f));
        //		if (ImGui::ImageButton(Icons["BugReport"]->TexHandle, ImVec2(30.f, 30.f)))
        //		{
        //#if USING( ME_PLATFORM_WIN64 )
        //			ShellExecute(0, 0, L"https://github.com/wobbier/MitchEngine/issues", 0, 0, SW_SHOW);
        //#endif
        //		}
        //		ImGui::PopStyleColor(1);

        ImGui::SetCursorPosX( ImGui::GetWindowWidth() - ( buttonWidth * 3.f ) + RightShift );
        if( ImGui::ImageButton( Icons["Minimize"]->TexHandle, ImVec2( 30.f, 30.f ) ) )
        {
            GetEngine().GetWindow()->Minimize();
        }

        if( GetEngine().GetWindow()->IsMaximized() )
        {
            ImGui::SetCursorPosX( ImGui::GetWindowWidth() - ( buttonWidth * 2.f ) + RightShift );
            if( ImGui::ImageButton( Icons["ExitMaximize"]->TexHandle, ImVec2( 30.f, 30.f ) ) )
            {
                GetEngine().GetWindow()->ExitMaximize();
            }
        }
        else
        {
            ImGui::SetCursorPosX( ImGui::GetWindowWidth() - ( buttonWidth * 2.f ) + RightShift );
            if( ImGui::ImageButton( Icons["Maximize"]->TexHandle, ImVec2( 30.f, 30.f ) ) )
            {
                GetEngine().GetWindow()->Maximize();
            }
        }

        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ACCENT_RED );
        ImGui::SetCursorPosX( ImGui::GetWindowWidth() - buttonWidth + RightShift );
        //ImGui::SameLine(0.f);
        if( ImGui::ImageButton( Icons["Close"]->TexHandle, ImVec2( 30.f, 30.f ) ) )
        {
            GetEngine().GetWindow()->Exit();
        }
        ImGui::PopStyleColor( 3 );
        ImGui::EndGroup();

        ImGui::EndMainMenuBar();
    }

    if( ShowAboutWindow )
    {
        ImGui::OpenPopup( "About" );
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos( center, ImGuiCond_Appearing, ImVec2( 0.5f, 0.5f ) );

    if( ImGui::BeginPopupModal( "About", &ShowAboutWindow, ImGuiWindowFlags_AlwaysAutoResize ) )
    {
        ImGui::Image( Icons["Logo"]->TexHandle, ImVec2( 35, 35 ) );
        ImGui::SameLine();
        ImGui::Text( "A hobby game engine project for making simple games.\nMitch Andrews 2023\n" );
        ImGui::Separator();
#if USING( ME_FMOD )
        ImGui::Text( "FMOD is set up correctly!\n\n" );
        if( !FMODImage )
        {
            FMODImage = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Legal/FMOD.png" ) );
        }
#endif

#if USING( ME_FMOD )
        ImGui::SetCursorPosX( ImGui::GetCursorPos().x + ( ImGui::GetContentRegionAvail().x - 364.f ) * 0.5f );
        ImGui::Image( FMODImage->TexHandle, { 364.f, 96.f } );
        ImGui::Text( "\n" );
#else
        ImGui::Text( "FMOD is not enabled! Please refer to Engine/README.md for instructions.\n" );
#endif

#if USING( ME_SCRIPTING )
        ImGui::Text( "MONO is set up correctly!\n\n" );
        if( !MONOImage )
        {
            MONOImage = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Legal/MONO.png" ) );
        }
#endif

#if USING( ME_SCRIPTING )
        ImGui::SetCursorPosX( ImGui::GetCursorPos().x + ( ImGui::GetContentRegionAvail().x - ( MONOImage->mWidth / 2 ) ) * 0.5f );
        ImGui::Image( MONOImage->TexHandle, { (float)MONOImage->mWidth / 2, (float)MONOImage->mHeight / 2 } );
        ImGui::Text( "\n" );
#else
        ImGui::Text( "MONO is not installed! Please refer to Engine/README.md for instructions." );
#endif

        ImGui::SetItemDefaultFocus();
        if( ImGui::Button( "Awesome!", ImVec2( -1, 0 ) ) ) {
            ShowAboutWindow = false; ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
#if USING( ME_FMOD )
    else
    {
        FMODImage = nullptr;
    }
#endif

    if( ShowDemoWindow )
    {
        ImGui::ShowDemoWindow( &ShowDemoWindow );
    }

    if( RequestLoadScene )
    {
        RequestAssetSelectionEvent evt( [this]( Path selectedAsset ) {
            LoadSceneEvent evt;
            evt.Level = selectedAsset.GetLocalPath();
            evt.Fire();

            GetEngine().GetConfig().SetValue( std::string( "CurrentScene" ), selectedAsset.GetLocalPath().data() );
            }, AssetType::Level );
        evt.Fire();
    }

    if( RequestSaveScene )
    {
        SaveSceneEvent evt;
        evt.Fire();
    }

    if( RequestSaveAsScene )
    {
        SaveSceneEvent evt;
        evt.SaveAs = true;
        evt.Fire();
    }
}

void MainMenuWidget::SetCallbacks( std::function<void()> StartGame, std::function<void()> PauseGame, std::function<void()> StopGame )
{
    StartGameFunc = StartGame;
    PauseGameFunc = PauseGame;
    StopGameFunc = StopGame;
}

#endif