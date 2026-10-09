#include "MainMenuWidget.h"
#include "Dementia.h"
#include <optick.h>
#include <Graphics/Texture.h>
#include <Resource/ResourceCache.h>
#include <Utils/ImGuiUtils.h>
#include <Events/SceneEvents.h>
#include <Engine/Engine.h>
#include "Editor/EditorActions.h"
#include "Editor/UndoStack.h"
#include <Utils/EditorConfig.h>
#include <EditorApp.h>
#include <Havana.h>
#include <Utils/StringUtils.h>
#include <Utils/PlatformUtils.h>
#include <Events/HavanaEvents.h>
#include <Window/SDLWindow.h>
#include "UI/Colors.h"
#include "Config.h"
#include "Events/EditorEvents.h"

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

void MainMenuWidget::SetData( std::vector<SharedPtr<HavanaWidget>>* widgetList, std::vector<SharedPtr<HavanaWidget>>* customWidgetList, EditorApp* editorApp )
{
    WidgetList = widgetList;
    CustomWidgetList = customWidgetList;
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
    EditorActions& actions = EditorActions::Get();

    ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 0.0f, 12.f ) );
    ImGui::PushStyleColor( ImGuiCol_MenuBarBg, COLOR_BACKGROUND_BORDER );
    if( ImGui::BeginMainMenuBar() )
    {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar( 1 );

        Input& editorInput = GetEngine().GetEditorInput();

        MainMenuSize = ImGui::GetWindowSize();
        ImGui::Image( Icons["Logo"]->TexHandle, ImVec2( 35, 35 ) );
        if( ImGui::BeginMenu( "File" ) )
        {
            actions.MenuItem( "File.NewScene" );
            actions.MenuItem( "File.OpenScene" );
            if( ImGui::BeginMenu( "Open Recent", !EditorConfig::GetInstance().RecentScenes.empty() ) )
            {
                for( const std::string& recent : EditorConfig::GetInstance().RecentScenes )
                {
                    if( ImGui::MenuItem( recent.c_str() ) )
                    {
                        App->RequestOpenScene( recent );
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            actions.MenuItem( "File.Save" );
            actions.MenuItem( "File.SaveAs" );
            ImGui::Separator();
            actions.MenuItem( "File.Quit" );
            ImGui::EndMenu();
        }

        if( ImGui::BeginMenu( "Edit" ) )
        {
            UndoStack& undo = UndoStack::Get();
            const std::string undoLabel = undo.CanUndo() ? "Undo " + undo.GetUndoName() : std::string( "Undo" );
            const std::string redoLabel = undo.CanRedo() ? "Redo " + undo.GetRedoName() : std::string( "Redo" );
            actions.MenuItem( "Edit.Undo", undoLabel.c_str() );
            actions.MenuItem( "Edit.Redo", redoLabel.c_str() );
            ImGui::Separator();
            actions.MenuItem( "Edit.Cut" );
            actions.MenuItem( "Edit.Copy" );
            actions.MenuItem( "Edit.Paste" );
            actions.MenuItem( "Edit.Duplicate" );
            actions.MenuItem( "Edit.Delete" );
            actions.MenuItem( "Edit.Rename" );
            ImGui::Separator();
            actions.MenuItem( "Edit.SelectAll" );
            actions.MenuItem( "Edit.Deselect" );
            ImGui::Separator();
            actions.MenuItem( "View.CommandPalette" );
            ImGui::EndMenu();
        }

        if( ImGui::BeginMenu( "Entity" ) )
        {
            actions.MenuItem( "Entity.CreateEmpty" );
            actions.MenuItem( "Entity.CreateEmptyChild" );
            ImGui::EndMenu();
        }

        if( ImGui::BeginMenu( "View" ) )
        {
            if( ImGui::BeginMenu( "Custom" ) )
            {
                if( CustomWidgetList )
                {
                    for( auto& i : *CustomWidgetList )
                    {
                        ImGui::MenuItem( i->Name.c_str(), i->Hotkey.c_str(), &i->IsOpen );
                    }
                }
                ImGui::EndMenu();
            }
            if( WidgetList )
            {
                for( auto& i : *WidgetList )
                {
                    if( i.get() != this )
                    {
                        ImGui::MenuItem( i->Name.c_str(), i->Hotkey.c_str(), &i->IsOpen );
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

        // Play controls.
        if( !App->IsGameRunning() )
        {
            if( ImGui::ImageButton( Icons["Play"]->TexHandle, ImVec2( 30.f, 30.f ) ) )
            {
                actions.Execute( "Play.Toggle" );
            }
            ImGui::SetItemTooltip( "Play (%s)", EditorActions::ShortcutToString( actions.Find( "Play.Toggle" )->Shortcut ).c_str() );
        }
        else
        {
            ImGui::PushStyleColor( ImGuiCol_Button, App->IsGamePaused() ? ImVec4( COLOR_PRIMARY ) : ImGui::GetStyleColorVec4( ImGuiCol_Button ) );
            if( ImGui::ImageButton( Icons["Pause"]->TexHandle, ImVec2( 30.f, 30.f ) ) )
            {
                actions.Execute( "Play.Pause" );
            }
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip( App->IsGamePaused() ? "Resume" : "Pause" );

            if( ImGui::ImageButton( Icons["Stop"]->TexHandle, ImVec2( 30.f, 30.f ) ) )
            {
                actions.Execute( "Play.Toggle" );
            }
            ImGui::SetItemTooltip( "Stop" );

            if( App->IsGamePaused() )
            {
                if( ImGui::Button( "Step", ImVec2( 0.f, 30.f ) ) )
                {
                    actions.Execute( "Play.Step" );
                }
            }

            float timeScale = GetEngine().GetTimeScale();
            ImGui::SetNextItemWidth( 70.f );
            if( ImGui::SliderFloat( "##TimeScale", &timeScale, 0.f, 4.f, "x%.2f" ) )
            {
                GetEngine().SetTimeScale( timeScale );
            }
            ImGui::SetItemTooltip( "Time scale (right-click resets)" );
            if( ImGui::IsItemClicked( ImGuiMouseButton_Right ) )
            {
                GetEngine().SetTimeScale( 1.f );
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
        const std::string title = UndoStack::Get().IsDirty() ? WindowTitle + " *" : WindowTitle;
        ImGui::TextUnformatted( title.c_str() );

        ImGui::BeginGroup();
        ImGui::PushStyleColor( ImGuiCol_Button, static_cast<ImVec4>( ImColor::HSV( 0.0f, 0.6f, 0.6f, 0.f ) ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonActive, static_cast<ImVec4>( ImColor::HSV( 0.f, 0.8f, 0.8f, 0.f ) ) );
#if USING( ME_OPTICK )
        ImGui::SetCursorPosX( ImGui::GetWindowWidth() - ( buttonWidth * 4.25f ) );
        if( ImGui::ImageButton( Icons["Profiler"]->TexHandle, ImVec2( 30.f, 30.f ) ) )
        {
            const Path optickPath = Path( "Engine/Tools/Optick.exe" );
            // additional information
            PlatformUtils::RunProcess( optickPath );
        }
#endif
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
        ImGui::Text( "C# is set up correctly! Enjoy!\n\n" );
        if( !DOTNETImage)
        {
            DOTNETImage = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Legal/DOTNET.png" ) );
        }
        ImGui::SetCursorPosX( ImGui::GetCursorPos().x + ( ImGui::GetContentRegionAvail().x - ( DOTNETImage->mWidth / 2 ) ) * 0.5f );
        ImGui::Image( DOTNETImage->TexHandle, { (float)DOTNETImage->mWidth / 2, (float)DOTNETImage->mHeight / 2 } );
        ImGui::Text( "\n" );
#else
        ImGui::Text( "DOTNET is not installed! Please refer to Engine/README.md for instructions." );
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

}


Vector2 MainMenuWidget::GetMainMenuSize() const
{
    return Vector2( MainMenuSize.x, MainMenuSize.y );
}

#endif