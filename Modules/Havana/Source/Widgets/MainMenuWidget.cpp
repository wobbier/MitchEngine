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
#include "Config.h"
#include "Events/EditorEvents.h"
#include "Widgets/Tree/TreeWidget.h"
#include <ImGuiTheme.h>

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

        const std::string workspaceLabel = "Workspace " + std::to_string( Editor->GetActiveWorkspace() );
        if( ImGui::BeginMenu( workspaceLabel.c_str() ) )
        {
            ImGui::TextDisabled( "Layouts and open panels are saved per workspace." );
            ImGui::Separator();
            for( int workspace = 1; workspace <= 9; ++workspace )
            {
                const std::string id = "Workspace.Select" + std::to_string( workspace );
                const EditorAction* action = actions.Find( id );
                const std::string label = "Workspace " + std::to_string( workspace );
                const std::string shortcut = action ? EditorActions::ShortcutToString( action->Shortcut ) : std::string();
                if( ImGui::MenuItem( label.c_str(), shortcut.c_str(), workspace == Editor->GetActiveWorkspace() ) ) actions.Execute( id );
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

        // Compact, consistently sized transport controls.
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( iconGap, 0.f ) );
        if( !App->IsGameRunning() )
        {
            if( iconButton( "Play", nullptr ) ) actions.Execute( "Play.Toggle" );
            ImGui::SetItemTooltip( "Play (%s)", EditorActions::ShortcutToString( actions.Find( "Play.Toggle" )->Shortcut ).c_str() );
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
            if( ImGui::SliderFloat( "##TimeScale", &timeScale, 0.f, 4.f, "x%.2f" ) ) GetEngine().SetTimeScale( timeScale );
            ImGui::SetItemTooltip( "Time scale (right-click resets)" );
            if( ImGui::IsItemClicked( ImGuiMouseButton_Right ) ) GetEngine().SetTimeScale( 1.f );
            ImGui::SetCursorPosY( menuY );
        }

        const float endOfMenu = ImGui::GetCursorPosX();
        const float buttonWidth = iconButtonSize + iconGap;
        const float winWidth = ImGui::GetWindowWidth();
        const float controlsLeft = winWidth - buttonWidth * 3.f - iconGap;
        float utilityLeft = controlsLeft;
#if USING( ME_OPTICK )
        utilityLeft -= buttonWidth;
#endif
        TitleBarDragPosition = Vector2( endOfMenu, 0.f );
        TitleBarDragSize = Vector2( std::max( 0.f, utilityLeft - endOfMenu ), MainMenuSize.y );
        const Vector2 pos = editorInput.GetMousePosition();
        if( ImGui::IsMouseDoubleClicked( 0 ) && pos > TitleBarDragPosition && pos < TitleBarDragPosition + TitleBarDragSize )
        {
            if( GetEngine().GetWindow()->IsMaximized() ) GetEngine().GetWindow()->ExitMaximize();
            else GetEngine().GetWindow()->Maximize();
        }

        // Clip long scene titles to the free space between menus and window buttons.
        const std::string title = UndoStack::Get().IsDirty() ? WindowTitle + " *" : WindowTitle;
        const float titleLeft = endOfMenu + spacing * 2.f, titleRight = utilityLeft - spacing * 1.5f;
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
