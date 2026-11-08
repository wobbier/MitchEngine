#include "MainMenuWidget.h"
#include <SDL.h>
#include "Renderer.h"
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
#include "Config.h"
#include "Events/EditorEvents.h"
#include "Widgets/Tree/TreeWidget.h"
#include <ImGuiTheme.h>
#include "Editor/NetPlay.h"
#include "Scripting/ScriptEngine.h"
#include "Core/WorkStatus.h"

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

    // The title bar is a menu bar with three times the frame padding; its icon buttons are
    // squares of one icon (a glyph's size) plus padding, centered vertically.
    const ImVec2 framePadding = ImGui::GetStyle().FramePadding;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float iconSize = ImGui::GetFontSize();
    const float iconPadding = ImGuiTheme::Em( 0.4f );
    const float iconButtonSize = iconSize + iconPadding * 2.f;
    const float iconGap = std::round( spacing * 0.5f );
    ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( spacing, framePadding.y * 3.f ) );
    const bool menuOpen = ImGui::BeginMainMenuBar();
    ImGui::PopStyleVar();
    if( menuOpen )
    {
        Input& editorInput = GetEngine().GetEditorInput();
        MainMenuSize = ImGui::GetWindowSize();
        const float menuY = ImGui::GetCursorPosY();
        const ImVec4 iconTint = ImGui::GetStyleColorVec4( ImGuiCol_Text );
        auto iconButton = [&]( const char* name, const char* tooltip ) {
            ImGui::SetCursorPosY( ( MainMenuSize.y - iconButtonSize ) * 0.5f );
            ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( iconPadding, iconPadding ) );
            const bool pressed = ImGui::ImageButton( name, ImGui::toId( Icons[name]->TexHandle, IMGUI_FLAGS_ALPHA_BLEND, 0 ), ImVec2( iconSize, iconSize ),
                ImVec2( 0, 0 ), ImVec2( 1, 1 ), ImVec4( 0, 0, 0, 0 ), iconTint );
            ImGui::PopStyleVar();
            if( tooltip ) ImGui::SetItemTooltip( "%s", tooltip );
            ImGui::SetCursorPosY( menuY );
            return pressed;
        };

        const float logoSize = ImGuiTheme::Em( 1.45f );
        ImGui::SetCursorPosY( ( MainMenuSize.y - logoSize ) * 0.5f );
        ImGui::Image( Icons["Logo"]->TexHandle, ImVec2( logoSize, logoSize ), ImVec2( 0, 0 ), ImVec2( 1, 1 ), iconTint );
        ImGui::SetCursorPosY( menuY );
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
            actions.MenuItem( "File.OpenProject" );
            actions.MenuItem( "File.ProjectHub" );
            ImGui::Separator();
            if( ImGui::BeginMenu( "New Shader Graph" ) )
            {
                actions.MenuItem( "ShaderGraph.NewLit", "Lit..." );
                actions.MenuItem( "ShaderGraph.NewUnlit", "Unlit..." );
                actions.MenuItem( "ShaderGraph.NewParticle", "Particle..." );
                actions.MenuItem( "ShaderGraph.NewDecal", "Decal..." );
                actions.MenuItem( "ShaderGraph.NewPostProcess", "Post Process..." );
                ImGui::EndMenu();
            }
            actions.MenuItem( "ShaderGraph.Open" );
            if( TreeWidget* trees = TreeWidget::Get() )
            {
                ImGui::Separator();
                trees->DrawFileMenuItems();
            }
            ImGui::Separator();
            actions.MenuItem( "File.Save" );
            actions.MenuItem( "File.SaveAs" );
            actions.MenuItem( "File.SaveAll" );
            ImGui::Separator();
            actions.MenuItem( "File.BuildGame" );
            actions.MenuItem( "File.BuildAndRun" );
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
            actions.MenuItem( "Edit.Preferences" );
            actions.MenuItem( "Edit.ProjectSettings" );
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
            if( TreeWidget* trees = TreeWidget::Get() )
            {
                ImGui::Separator();
                trees->DrawViewMenu();
            }
            ImGui::EndMenu();
        }

        if( ImGui::BeginMenu( "Help" ) )
        {
            // ED-3: the docs, the tour and the release notes (Help.* actions; F1 opens the docs).
            if( ImGui::MenuItem( "Documentation", "F1" ) ) EditorActions::Get().Execute( "Help.Documentation" );
            if( ImGui::MenuItem( "Getting Started" ) ) EditorActions::Get().Execute( "Help.GettingStarted" );
            if( ImGui::MenuItem( "Take the Tour" ) ) EditorActions::Get().Execute( "Help.Tour" );
            if( ImGui::MenuItem( "What's New" ) ) EditorActions::Get().Execute( "Help.WhatsNew" );
            ImGui::Separator();
            // ME-61: whether RenderDoc is hooked up, and how to get it when it isn't.
            if( ImGui::BeginMenu( "RenderDoc" ) )
            {
                BGFXRenderer& renderer = GetEngine().GetRenderer();
                if( renderer.IsRenderDocLoaded() )
                {
                    ImGui::TextColored( ImVec4( 0.4f, 0.9f, 0.4f, 1.f ), "Loaded: captures go to RenderDoc's capture folder" );
                    if( ImGui::MenuItem( "Capture Next Frame" ) )
                    {
                        renderer.CaptureRenderDocFrame();
                    }
                }
                else
                {
                    ImGui::TextDisabled( "Not loaded" );
#if USING( ME_PLATFORM_WINDOWS )
                    ImGui::TextUnformatted( "Install RenderDoc to C:/Program Files/RenderDoc," );
                    ImGui::TextUnformatted( "then regenerate the solution (it enables DEFINE_ME_RENDERDOC)." );
#else
                    ImGui::TextUnformatted( "The in-app API is Windows-only. Here, launch Havana from" );
                    ImGui::TextUnformatted( "RenderDoc (qrenderdoc: Launch Application, Vulkan) to capture." );
#endif
                }
                ImGui::Separator();
                if( ImGui::MenuItem( "RenderDoc Website" ) )
                {
                    SDL_OpenURL( "https://renderdoc.org" );
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
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

        // Compact, consistently sized transport controls.
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( iconGap, 0.f ) );
        if( !App->IsGameRunning() )
        {
            if( iconButton( "Play", nullptr ) ) actions.Execute( "Play.Toggle" );
            ImGui::SetItemTooltip( "Play (%s)", EditorActions::ShortcutToString( actions.Find( "Play.Toggle" )->Shortcut ).c_str() );
            ImGui::SetCursorPosY( ( MainMenuSize.y - iconButtonSize ) * 0.5f );
            if( ImGui::Button( "Simulate", ImVec2( 0.f, iconButtonSize ) ) ) actions.Execute( "Play.Simulate" );
            ImGui::SetItemTooltip( "Play without scripts: physics, animation and particles run" );
            // NET-7: Play over the network (a listen server here, child client Players).
            ImGui::SetCursorPosY( ( MainMenuSize.y - iconButtonSize ) * 0.5f );
            NetPlay::Get().DrawToolbar( false );
            ImGui::SetCursorPosY( menuY );
        }
        else
        {
            ImGui::PushStyleColor( ImGuiCol_Button, App->IsGamePaused() ? ImGui::GetStyleColorVec4( ImGuiCol_HeaderActive ) : ImGui::GetStyleColorVec4( ImGuiCol_Button ) );
            if( iconButton( "Pause", App->IsGamePaused() ? "Resume" : "Pause" ) ) actions.Execute( "Play.Pause" );
            ImGui::PopStyleColor();
            if( iconButton( "Stop", "Stop" ) ) actions.Execute( "Play.Toggle" );

            if( App->IsGamePaused() )
            {
                ImGui::SetCursorPosY( ( MainMenuSize.y - iconButtonSize ) * 0.5f );
                if( ImGui::Button( "Step", ImVec2( 0.f, iconButtonSize ) ) ) actions.Execute( "Play.Step" );
                ImGui::SetItemTooltip( "Advance one frame" );
                ImGui::SetCursorPosY( menuY );
            }

            float timeScale = GetEngine().GetTimeScale();
            ImGui::SetCursorPosY( ( MainMenuSize.y - ImGui::GetFrameHeight() ) * 0.5f );
            ImGui::SetNextItemWidth( ImGuiTheme::Em( 4.25f ) );
            // NET-7: the PIE clients run on wall-clock time, so a networked Play stays at x1.
            const bool netPlay = NetPlay::Get().IsHosting();
            ImGui::BeginDisabled( netPlay );
            if( ImGui::SliderFloat( "##TimeScale", &timeScale, 0.f, 4.f, "x%.2f" ) ) GetEngine().SetTimeScale( timeScale );
            ImGui::EndDisabled();
            ImGui::SetItemTooltip( netPlay ? "Time scale: x1 while clients play (their clocks run on wall time)" : "Time scale (right-click resets)" );
            if( !netPlay && ImGui::IsItemClicked( ImGuiMouseButton_Right ) ) GetEngine().SetTimeScale( 1.f );
            if( NetPlay::Get().IsHosting() )
            {
                ImGui::SetCursorPosY( ( MainMenuSize.y - ImGui::GetFrameHeight() ) * 0.5f );
                NetPlay::Get().DrawToolbar( true );
            }
            ImGui::SetCursorPosY( menuY );
        }

        const float buttonWidth = iconButtonSize + iconGap;
        if( Editor )
        {
            // Room for the window buttons and a little of the scene title.
            ImGui::SetCursorPosX( ImGui::GetCursorPosX() + spacing * 2.f );
            DrawWorkspaceTabs( iconButtonSize, ImGui::GetWindowWidth() - ImGui::GetCursorPosX() - buttonWidth * 4.f - ImGuiTheme::Em( 14.f ) );
            ImGui::SetCursorPosY( menuY );
        }
        const float endOfMenu = ImGui::GetCursorPosX();
        const float winWidth = ImGui::GetWindowWidth();
        const float controlsLeft = winWidth - buttonWidth * 3.f - iconGap;
        float utilityLeft = controlsLeft;
#if USING( ME_OPTICK )
        utilityLeft -= buttonWidth;
#endif
        // ME-233: a failed script build's error count, left of the utilities; opens the Error List.
        float titleBarRight = utilityLeft;
        std::string errorBadge;
#if USING( ME_SCRIPTING )
        if( const size_t scriptErrors = ScriptEngine::GetBuildErrorCount() )
        {
            errorBadge = std::to_string( scriptErrors ) + ( scriptErrors == 1 ? " C# error" : " C# errors" );
            titleBarRight -= ImGui::CalcTextSize( errorBadge.c_str() ).x + ImGui::GetStyle().FramePadding.x * 2.f + spacing;
        }
#endif
        // ED-14: what's working in the background (script builds, imports, cooks, bakes).
        const std::vector<WorkStatus::Task> work = WorkStatus::Get().GetTasks();
        std::string workText;
        float workLeft = titleBarRight;
        if( !work.empty() )
        {
            static const char* kSpinner[] = { "|", "/", "-", "\\" };
            const int frame = static_cast<int>( ImGui::GetTime() * 8.0 ) % 4;
            workText = std::string( kSpinner[frame] ) + " " + work.front().Label;
            if( work.front().Progress >= 0.f )
            {
                workText += " " + std::to_string( static_cast<int>( work.front().Progress * 100.f ) ) + "%";
            }
            if( work.size() > 1 )
            {
                workText += " +" + std::to_string( work.size() - 1 );
            }
            workLeft = titleBarRight - ImGui::CalcTextSize( workText.c_str() ).x - spacing * 2.f;
            titleBarRight = workLeft;
        }
        TitleBarDragPosition = Vector2( endOfMenu, 0.f );
        TitleBarDragSize = Vector2( std::max( 0.f, titleBarRight - endOfMenu ), MainMenuSize.y );
        const Vector2 pos = editorInput.GetMousePosition();
        if( ImGui::IsMouseDoubleClicked( 0 ) && pos > TitleBarDragPosition && pos < TitleBarDragPosition + TitleBarDragSize )
        {
            if( GetEngine().GetWindow()->IsMaximized() ) GetEngine().GetWindow()->ExitMaximize();
            else GetEngine().GetWindow()->Maximize();
        }

        // Clip long scene titles to the free space between menus and window buttons.
        const std::string title = UndoStack::Get().IsDirty() ? WindowTitle + " *" : WindowTitle;
        const float titleLeft = endOfMenu + spacing * 2.f, titleRight = titleBarRight - spacing * 1.5f;
        if( titleRight > titleLeft )
        {
            const ImVec2 origin = ImGui::GetWindowPos();
            const float titleX = std::max( titleLeft, ( winWidth - ImGui::CalcTextSize( title.c_str() ).x ) * 0.5f );
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect( ImVec2( origin.x + titleLeft, origin.y ), ImVec2( origin.x + titleRight, origin.y + MainMenuSize.y ), true );
            drawList->AddText( ImVec2( origin.x + titleX, origin.y + ( MainMenuSize.y - ImGui::GetFontSize() ) * 0.5f ), ImGui::GetColorU32( ImGuiCol_TextDisabled ), title.c_str() );
            drawList->PopClipRect();
        }

        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0, 0, 0, 0 ) );
        if( !workText.empty() )
        {
            ImGui::SetCursorPosX( workLeft );
            ImGui::SetCursorPosY( ( MainMenuSize.y - ImGui::GetTextLineHeight() ) * 0.5f );
            ImGui::TextDisabled( "%s", workText.c_str() );
            if( ImGui::IsItemHovered() )
            {
                ImGui::BeginTooltip();
                for( const WorkStatus::Task& task : work )
                {
                    if( task.Progress >= 0.f )
                    {
                        ImGui::Text( "%s: %d%%", task.Label.c_str(), static_cast<int>( task.Progress * 100.f ) );
                    }
                    else
                    {
                        ImGui::TextUnformatted( task.Label.c_str() );
                    }
                }
                ImGui::EndTooltip();
            }
            ImGui::SetCursorPosY( menuY );
        }
        if( !errorBadge.empty() )
        {
            ImGui::SetCursorPosX( titleBarRight );
            ImGui::SetCursorPosY( ( MainMenuSize.y - ImGui::GetFrameHeight() ) * 0.5f );
            ImGui::PushStyleColor( ImGuiCol_Text, ImGuiTheme::ErrorText() );
            if( ImGui::Button( errorBadge.c_str() ) && Editor )
            {
                Editor->ShowWidget( "Error List" );
            }
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip( "The scripts don't build, so Play waits: open the Error List" );
            ImGui::SetCursorPosY( menuY );
        }
#if USING( ME_OPTICK )
        ImGui::SetCursorPosX( utilityLeft );
        if( iconButton( "Profiler", "Open Optick profiler" ) ) PlatformUtils::RunProcess( Path( "Engine/Tools/Optick.exe" ) );
#endif
        ImGui::SetCursorPosX( controlsLeft );
        if( iconButton( "Minimize", "Minimize" ) ) GetEngine().GetWindow()->Minimize();
        ImGui::SetCursorPosX( controlsLeft + buttonWidth );
        if( GetEngine().GetWindow()->IsMaximized() )
        {
            if( iconButton( "ExitMaximize", "Restore window" ) ) GetEngine().GetWindow()->ExitMaximize();
        }
        else if( iconButton( "Maximize", "Maximize" ) ) GetEngine().GetWindow()->Maximize();

        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImGuiTheme::WithAlpha( ImGuiTheme::ErrorText(), 0.35f ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImGuiTheme::WithAlpha( ImGuiTheme::ErrorText(), 0.5f ) );
        ImGui::SetCursorPosX( controlsLeft + buttonWidth * 2.f );
        if( iconButton( "Close", "Close editor" ) ) GetEngine().GetWindow()->Exit();
        ImGui::PopStyleColor( 3 );
        ImGui::PopStyleVar();
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
        ImGui::Image( Icons["Logo"]->TexHandle, ImVec2( ImGuiTheme::Em( 2.f ), ImGuiTheme::Em( 2.f ) ), ImVec2( 0, 0 ), ImVec2( 1, 1 ), ImGui::GetStyleColorVec4( ImGuiCol_Text ) );
        ImGui::SameLine();
        ImGui::Text( "MitchEngine and the Havana editor: C# games, from the first hour to a shipped build.\nMitch Andrews, 2017-2026\n" );
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

void MainMenuWidget::DrawWorkspaceTabs( float InHeight, float InMaxWidth )
{
    EditorActions& actions = EditorActions::Get();
    const std::vector<Havana::Workspace> workspaces = Editor->GetWorkspaces();     // a copy: the menus below change the list
    const int active = Editor->GetActiveWorkspace();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float barY = ImGui::GetCursorPosY();
    const float tabY = ( MainMenuSize.y - InHeight ) * 0.5f;

    const float plusWidth = ImGui::CalcTextSize( "+" ).x + style.FramePadding.x * 2.f;
    float stripWidth = plusWidth;
    for( const Havana::Workspace& workspace : workspaces )
    {
        stripWidth += ImGui::CalcTextSize( workspace.Name.c_str() ).x + style.FramePadding.x * 2.f + style.ItemSpacing.x;
    }
    const bool compact = stripWidth > InMaxWidth;

    // The rename field stands in for its tab; Enter or clicking away keeps the name, Escape doesn't.
    const auto drawRename = [this]( const Havana::Workspace& InWorkspace, float InY ) {
        const bool starting = m_workspaceRenameFocus == InWorkspace.Id;
        if( starting )
        {
            std::snprintf( m_workspaceName, sizeof( m_workspaceName ), "%s", InWorkspace.Name.c_str() );
            ImGui::SetKeyboardFocusHere();
            m_workspaceRenameFocus = 0;
        }
        ImGui::SetCursorPosY( InY );
        ImGui::SetNextItemWidth( std::max( ImGuiTheme::Em( 7.f ), ImGui::CalcTextSize( m_workspaceName ).x + ImGuiTheme::Em( 1.5f ) ) );
        const bool enter = ImGui::InputText( "##WorkspaceName", m_workspaceName, sizeof( m_workspaceName ), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll );
        // Escape has already put the old name back in the field. A field that never took focus ends
        // with the next click elsewhere.
        if( enter || ImGui::IsItemDeactivated() )
        {
            Editor->RenameWorkspace( InWorkspace.Id, m_workspaceName );
            Editor->RenamingWorkspace = 0;
        }
        else if( !starting && !ImGui::IsItemActive() && !ImGui::IsItemHovered() && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
        {
            Editor->RenamingWorkspace = 0;
        }
    };
    const auto startRename = [this]( int InId ) { Editor->RenamingWorkspace = InId; };
    // A rename just started (here, or Workspace.Rename from the palette): its field takes focus.
    if( Editor->RenamingWorkspace != m_workspaceRenaming )
    {
        m_workspaceRenaming = Editor->RenamingWorkspace;
        m_workspaceRenameFocus = m_workspaceRenaming;
    }

    int moveId = 0, moveTo = -1;
    if( compact )
    {
        const Havana::Workspace* current = Editor->FindWorkspace( active );
        ImGui::PushID( "WorkspaceTabs" );
        if( current && Editor->RenamingWorkspace == active )
        {
            drawRename( *current, ( MainMenuSize.y - ImGui::GetFrameHeight() ) * 0.5f );
        }
        else
        {
            ImGui::SetCursorPosY( tabY );
            const std::string label = ( current ? current->Name : std::string( "Workspace" ) ) + " v";
            if( ImGui::Button( label.c_str(), ImVec2( 0.f, InHeight ) ) ) ImGui::OpenPopup( "##WorkspaceList" );
            ImGui::SetItemTooltip( "Workspace (Ctrl+1..9)" );
        }
        if( ImGui::BeginPopup( "##WorkspaceList" ) )
        {
            for( size_t i = 0; i < workspaces.size(); ++i )
            {
                const std::string shortcut = i < 9 ? EditorActions::ShortcutToString( actions.Find( "Workspace.Select" + std::to_string( i + 1 ) )->Shortcut ) : std::string();
                if( ImGui::MenuItem( workspaces[i].Name.c_str(), shortcut.c_str(), workspaces[i].Id == active ) ) Editor->SwitchWorkspace( workspaces[i].Id );
            }
            ImGui::Separator();
            if( ImGui::MenuItem( "Rename..." ) ) startRename( active );
            if( ImGui::MenuItem( "Move Left", nullptr, false, Editor->GetWorkspaceIndex( active ) > 0 ) ) Editor->MoveWorkspace( active, Editor->GetWorkspaceIndex( active ) - 1 );
            if( ImGui::MenuItem( "Move Right", nullptr, false, Editor->GetWorkspaceIndex( active ) + 1 < static_cast<int>( workspaces.size() ) ) ) Editor->MoveWorkspace( active, Editor->GetWorkspaceIndex( active ) + 1 );
            if( ImGui::BeginMenu( "New Workspace" ) )
            {
                if( ImGui::MenuItem( "Duplicate Current" ) ) Editor->NewWorkspace( "" );
                ImGui::Separator();
                for( const std::string& name : Havana::GetWorkspaceTemplates() )
                {
                    if( ImGui::MenuItem( name.c_str() ) ) Editor->NewWorkspace( name );
                }
                ImGui::EndMenu();
            }
            if( ImGui::MenuItem( "Reset Layout" ) ) Editor->ResetLayout();
            if( ImGui::MenuItem( "Delete", nullptr, false, workspaces.size() > 1 ) ) Editor->DeleteWorkspace( active );
            ImGui::EndPopup();
        }
        ImGui::PopID();
        return;
    }

    // Opened from inside each tab's PushID, so it's opened by this ID, not by name.
    const ImGuiID workspaceMenu = ImGui::GetID( "##WorkspaceMenu" );
    ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( style.ItemSpacing.x * 0.5f, 0.f ) );
    for( size_t i = 0; i < workspaces.size(); ++i )
    {
        const Havana::Workspace& workspace = workspaces[i];
        ImGui::PushID( workspace.Id );
        if( Editor->RenamingWorkspace == workspace.Id )
        {
            drawRename( workspace, ( MainMenuSize.y - ImGui::GetFrameHeight() ) * 0.5f );
            ImGui::PopID();
            continue;
        }
        const bool selected = workspace.Id == active;
        ImGui::PushStyleColor( ImGuiCol_Button, selected ? ImGui::GetStyleColorVec4( ImGuiCol_TabActive ) : ImVec4( 0, 0, 0, 0 ) );
        ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( selected ? ImGuiCol_Text : ImGuiCol_TextDisabled ) );
        ImGui::SetCursorPosY( tabY );
        const bool pressed = ImGui::Button( workspace.Name.c_str(), ImVec2( 0.f, InHeight ) );
        ImGui::PopStyleColor( 2 );
        if( ImGui::IsItemActivated() ) m_workspaceTabDragged = false;
        // Dragged past a neighbour, it swaps with it (the tab stays under the mouse).
        if( ImGui::IsItemActive() && !ImGui::IsItemHovered() && ImGui::IsMouseDragging( ImGuiMouseButton_Left ) )
        {
            const float mouseX = ImGui::GetIO().MousePos.x;
            const int step = mouseX < ImGui::GetItemRectMin().x ? -1 : mouseX > ImGui::GetItemRectMax().x ? 1 : 0;
            const int to = static_cast<int>( i ) + step;
            if( step != 0 && to >= 0 && to < static_cast<int>( workspaces.size() ) )
            {
                moveId = workspace.Id;
                moveTo = to;
                m_workspaceTabDragged = true;
            }
        }
        if( pressed && !m_workspaceTabDragged ) Editor->SwitchWorkspace( workspace.Id );
        if( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) ) startRename( workspace.Id );
        if( ImGui::IsItemClicked( ImGuiMouseButton_Right ) )
        {
            m_workspaceMenuFor = workspace.Id;
            ImGui::OpenPopup( workspaceMenu );
        }
        if( ImGui::IsItemHovered( ImGuiHoveredFlags_DelayNormal ) && !ImGui::IsItemActive() )
        {
            const std::string shortcut = i < 9 ? EditorActions::ShortcutToString( actions.Find( "Workspace.Select" + std::to_string( i + 1 ) )->Shortcut ) : std::string();
            ImGui::SetTooltip( "%s%s%s (%s layout)\nDrag to reorder, double-click to rename, right-click for more", workspace.Name.c_str(),
                shortcut.empty() ? "" : "  ", shortcut.c_str(), workspace.Template.c_str() );
        }
        ImGui::PopID();
    }
    ImGui::PopStyleVar();

    // The tab's menu (one popup for all of them, so it outlives a reorder).
    if( ImGui::BeginPopup( "##WorkspaceMenu" ) )
    {
        const int id = m_workspaceMenuFor;
        const int index = Editor->GetWorkspaceIndex( id );
        if( index < 0 )
        {
            ImGui::CloseCurrentPopup();
        }
        else
        {
            ImGui::TextDisabled( "%s", workspaces[index].Name.c_str() );
            ImGui::Separator();
            if( ImGui::MenuItem( "Rename" ) ) startRename( id );
            if( ImGui::MenuItem( "Duplicate" ) )
            {
                Editor->SwitchWorkspace( id );
                Editor->NewWorkspace( "" );
            }
            if( ImGui::MenuItem( "Reset Layout" ) )
            {
                Editor->SwitchWorkspace( id );
                Editor->ResetLayout();
            }
            ImGui::Separator();
            if( ImGui::MenuItem( "Move Left", nullptr, false, index > 0 ) ) Editor->MoveWorkspace( id, index - 1 );
            if( ImGui::MenuItem( "Move Right", nullptr, false, index + 1 < static_cast<int>( workspaces.size() ) ) ) Editor->MoveWorkspace( id, index + 1 );
            ImGui::Separator();
            if( ImGui::MenuItem( "Delete", nullptr, false, workspaces.size() > 1 ) ) Editor->DeleteWorkspace( id );
        }
        ImGui::EndPopup();
    }

    ImGui::SetCursorPosY( tabY );
    ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0, 0, 0, 0 ) );
    if( ImGui::Button( "+##NewWorkspace", ImVec2( 0.f, InHeight ) ) ) ImGui::OpenPopup( "##NewWorkspace" );
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip( "New workspace" );
    if( ImGui::BeginPopup( "##NewWorkspace" ) )
    {
        if( const Havana::Workspace* current = Editor->FindWorkspace( active ) )
        {
            const std::string duplicate = "Duplicate " + current->Name;
            if( ImGui::MenuItem( duplicate.c_str() ) ) Editor->NewWorkspace( "" );
        }
        ImGui::Separator();
        ImGui::TextDisabled( "From a layout" );
        for( const std::string& name : Havana::GetWorkspaceTemplates() )
        {
            if( ImGui::MenuItem( name.c_str() ) ) Editor->NewWorkspace( name );
        }
        ImGui::EndPopup();
    }
    ImGui::SetCursorPosY( barY );

    if( moveId ) Editor->MoveWorkspace( moveId, moveTo );
}

#endif
