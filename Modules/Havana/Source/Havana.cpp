#include "Havana.h"
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <imgui.h>

#include "Engine/World.h"
#include "Components/Transform.h"
#include "Components/Camera.h"
#include "ECS/Core.h"
#include "Engine/Engine.h"

#include "Events/HavanaEvents.h"
#include "Events/SceneEvents.h"
#if USING( ME_EDITOR )
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
#include <Renderer.h>
#include <Window/SDLWindow.h>
#include <backends/imgui_impl_sdl2.h>
#include <Utils/EditorConfig.h>
#include "HavanaWidget.h"
#include "Editor/WidgetRegistry.h"
#include "Window/IWindow.h"
#include "Widgets/HistoryWidget.h"
#include "Widgets/ErrorListWidget.h"
#include "Widgets/LocalizationWidget.h"
#include "Widgets/OnboardingWidgets.h"
#include "Widgets/UIDesigner/UIDesignerWidget.h"
#include "Utils/PlatformUtils.h"
#include "Core/WorkStatus.h"
#include <ImGuiTheme.h>
#include "Engine/Project.h"
#include "Editor/Selection.h"
#include "Widgets/SequenceWidget.h"
#include "Widgets/ProfilerWidget.h"
#include "Widgets/SettingsWidgets.h"
#include "Widgets/BuildGameWidget.h"
#include "Widgets/OpenProjectWidget.h"
#include "Widgets/ShaderGraph/ShaderGraphWidget.h"
#include "Widgets/Terrain/TerrainWidget.h"
#include "Widgets/Tree/TreeWidget.h"
#include "Utils/EditorIcons.h"
#include "Editor/EditorActions.h"
#include "Editor/EditorOperations.h"
#include "Editor/DefaultEditorActions.h"
#include "Editor/ReflectionUI.h"
#include "Engine/AutomationRunner.h"
#include <imgui_internal.h>
#include <cstring>

static SDL_Cursor* g_imgui_to_sdl_cursor[ImGuiMouseCursor_COUNT];

Havana::Havana( Engine* GameEngine, EditorApp* app )
    : m_engine( GameEngine )
    , m_app( app )
{
//	m_assetBrowser.Start([](const std::string& path_to_watch, FileStatus status) -> void {
//		// Process only regular files, all other file types are ignored
//		if (!std::filesystem::is_regular_file(std::filesystem::path(path_to_watch)) && status != FileStatus::Deleted)
//		{
//			return;
//		}
//
//		switch (status) {
//		case FileStatus::Created:
//		{
//#if USING( ME_PLATFORM_WIN64 )
//			CLog::GetInstance().Log(CLog::LogType::Info, "File created: " + path_to_watch);
//			TestEditorEvent evt;
//			evt.Path = std::move(path_to_watch);
//			evt.Queue();
//#endif
//		}
//		break;
//		case FileStatus::Modified:
//			CLog::GetInstance().Log(CLog::LogType::Info, "File modified: " + path_to_watch);
//			break;
//		case FileStatus::Deleted:
//			CLog::GetInstance().Log(CLog::LogType::Info, "File deleted: " + path_to_watch);
//			break;
//		default:
//			CLog::GetInstance().Log(CLog::LogType::Error, "Error! Unknown file status.");
//		}
//		});
    Listen<ModelHierarchyEvent>( []( const ModelHierarchyEvent& InEvent ) {
        if( InEvent.Target )
        {
            if( InEvent.NodePath.empty() ) EditorOps::CompactModel( *InEvent.Target.Get() );
            else EditorOps::ExposeModelNode( *InEvent.Target.Get(), InEvent.NodePath );
        }
        return true;
    } );
    Listen<LoadSceneEvent>( []( const LoadSceneEvent& ) { ClearInspectEvent().Fire(); } );
    Renderer = &GameEngine->GetRenderer();

    MainMenu.reset( new MainMenuWidget( this ) );
    RegisteredWidgets.push_back( MainMenu );

    AssetBrowser.reset( new AssetBrowserWidget( this ) );
    RegisteredWidgets.push_back( AssetBrowser );

    LogPanel.reset( new LogWidget() );
    RegisteredWidgets.push_back( LogPanel );

    ResourceMonitor.reset( new ResourceMonitorWidget() );
    RegisteredWidgets.push_back( ResourceMonitor );

    MainSceneView.reset( new SceneViewWidget( "World View", true ) );
    RegisteredWidgets.push_back( MainSceneView );
    GameSceneView.reset( new SceneViewWidget( "Game View" ) );
    RegisteredWidgets.push_back( GameSceneView );

   // ME_ASSERT( false, "MainSceneView->App should be nullptr" );;
    SceneHierarchy.reset( new SceneHierarchyWidget() );
    RegisteredWidgets.push_back( SceneHierarchy );

    PropertiesView.reset( new PropertiesWidget() );
    RegisteredWidgets.push_back( PropertiesView );

    AssetPreview.reset( new AssetPreviewWidget() );
    RegisteredWidgets.push_back( AssetPreview );

    History.reset( new HistoryWidget() );
    RegisteredWidgets.push_back( History );
    ErrorList.reset( new ErrorListWidget() );
    RegisteredWidgets.push_back( ErrorList );
    LocalizationTables.reset( new LocalizationWidget() );
    RegisteredWidgets.push_back( LocalizationTables );
    Tour.reset( new TourWidget() );
    // The step's panel comes to the front (its dock tab selected), and the tour keeps the keyboard.
    Tour->ShowPanel = [this]( const std::string& InPanel ) {
        ShowWidget( InPanel );
        ImGui::SetWindowFocus( "Tour" );
    };
    RegisteredWidgets.push_back( Tour );
    WhatsNew.reset( new WhatsNewWidget() );
    RegisteredWidgets.push_back( WhatsNew );
    UIDesigner.reset( new UIDesignerWidget() );
    RegisteredWidgets.push_back( UIDesigner );

    Sequencer.reset( new SequenceWidget() );
    RegisteredWidgets.push_back( Sequencer );

    Profiler.reset( new ProfilerWidget() );
    RegisteredWidgets.push_back( Profiler );

    Preferences.reset( new PreferencesWidget() );
    RegisteredWidgets.push_back( Preferences );

    ProjectSettingsView.reset( new ProjectSettingsWidget() );
    RegisteredWidgets.push_back( ProjectSettingsView );

    BuildGameView.reset( new BuildGameWidget() );
    RegisteredWidgets.push_back( BuildGameView );

    OpenProjectView.reset( new OpenProjectWidget( app ) );
    RegisteredWidgets.push_back( OpenProjectView );

    TerrainPanel.reset( new TerrainWidget() );
    RegisteredWidgets.push_back( TerrainPanel );
    ShaderGraphs.reset( new ShaderGraphWidget() );
    Trees.reset( new TreeWidget() );

    EditorActions& actions = EditorActions::Get();
    actions.IsContextActive = [this]( ActionContext context ) {
        return context != ActionContext::Scene || IsSceneContextFocused();
    };
    actions.IsGameFocused = [this]() {
        return m_app->IsGameRunning() && GetEngine().GetInput().IsCapturing();
    };
    PreferencesWidget::ApplyStartupPreferences();
    RegisterDefaultEditorActions( *app );
    actions.Register( { "Edit.Preferences", "Preferences...", "Edit", ImGuiMod_Shortcut | ImGuiKey_Comma, 0, [this]() { ShowWidget( "Preferences" ); } } );
    actions.Register( { "Edit.ProjectSettings", "Project Settings...", "Edit", 0, 0, [this]() { ShowWidget( "Project Settings" ); } } );
    actions.Register( { "File.BuildGame", "Build Game...", "File", ImGuiMod_Shortcut | ImGuiMod_Shift | ImGuiKey_B, 0, [this]() { ShowWidget( "Build Game" ); } } );
    actions.Register( { "File.OpenProject", "Open Project...", "File", 0, 0, [this]() { ShowWidget( "Open Project" ); } } );
    actions.Register( { "File.ProjectHub", "Project Hub...", "File", 0, 0, [app]() { app->RequestProjectHub(); } } );
    actions.Register( { "View.Profiler", "Profiler", "View", 0, 0, [this]() { ShowWidget( "Profiler" ); } } );
    actions.Register( { "View.ErrorList", "Error List", "View", 0, 0, [this]() { ShowWidget( "Error List" ); } } );
    ReflectionUI::RegisterDefaultDrawers();
    actions.Register( { "View.AssetSearch", "Quick Asset Search", "View", ImGuiMod_Shortcut | ImGuiKey_Space, 0, [this]() { AssetBrowser->RequestOverlay( nullptr ); } } );
    // ED-15: the command palette also finds entities, assets, settings and docs.
    actions.SearchProviders.push_back( []( std::vector<EditorActions::PaletteItem>& OutItems ) {
        SharedPtr<World> world = GetEngine().GetWorld().lock();
        if( !world )
        {
            return;
        }
        world->ForEachEntity( [&]( Entity& InEntity ) {
            if( world->IsSingletonEntity( InEntity.GetId() ) || InEntity.GetName().empty() )
            {
                return;
            }
            EntityHandle handle = InEntity.GetHandle();
            OutItems.push_back( { "Entity", InEntity.GetName(), [handle]() {
                if( handle )
                {
                    Selection::Get().Clear();
                    Selection::Get().Add( handle );
                    EditorActions::Get().Execute( "View.FrameSelected" );
                }
            } } );
        } );
    } );
    actions.SearchProviders.push_back( [this]( std::vector<EditorActions::PaletteItem>& OutItems ) {
        // The project's files (sources, not cooked or editor sidecars), rescanned every few seconds.
        static std::vector<std::string> s_assets;
        static double s_scannedAt = -100.0;
        const double now = ImGui::GetTime();
        if( now - s_scannedAt > 5.0 )
        {
            s_scannedAt = now;
            s_assets.clear();
            std::error_code error;
            const std::filesystem::path root( Path( "Assets" ).FullPath );
            for( auto it = std::filesystem::recursive_directory_iterator( root, std::filesystem::directory_options::skip_permission_denied, error );
                 it != std::filesystem::recursive_directory_iterator(); it.increment( error ) )
            {
                if( error || !it->is_regular_file( error ) )
                {
                    continue;
                }
                const std::string extension = it->path().extension().string();
                if( extension == ".meta" || extension == ".dds" || extension == ".bin" || extension == ".assbin" || extension == ".navmesh" )
                {
                    continue;
                }
                s_assets.push_back( "Assets/" + std::filesystem::relative( it->path(), root, error ).generic_string() );
            }
        }
        for( const std::string& asset : s_assets )
        {
            OutItems.push_back( { "Asset", asset, [this, asset]() {
                ShowWidget( "Asset Browser" );
                if( AssetBrowser )
                {
                    AssetBrowser->ShowAsset( asset );
                }
            } } );
        }
    } );
    actions.SearchProviders.push_back( [this]( std::vector<EditorActions::PaletteItem>& OutItems ) {
        for( const char* tab : { "Appearance", "General", "Scene View", "Shortcuts" } )
        {
            OutItems.push_back( { "Setting", std::string( "Preferences > " ) + tab, [this]() { ShowWidget( "Preferences" ); } } );
        }
        for( const char* tab : { "Time", "Project", "Hardware", "Look", "Layers", "Physics", "Input", "Audio", "Terrain", "Navigation" } )
        {
            OutItems.push_back( { "Setting", std::string( "Project Settings > " ) + tab, [this]() { ShowWidget( "Project Settings" ); } } );
        }
    } );
    actions.SearchProviders.push_back( []( std::vector<EditorActions::PaletteItem>& OutItems ) {
        std::error_code error;
        const std::filesystem::path docs = std::filesystem::path( Project::GetEngineRoot() ) / "Docs";
        for( const auto& entry : std::filesystem::directory_iterator( docs, error ) )
        {
            if( entry.path().extension() == ".md" )
            {
                const std::string file = entry.path().generic_string();
                OutItems.push_back( { "Doc", entry.path().stem().string(), [file]() { PlatformUtils::OpenInCodeEditor( file, 1 ); } } );
            }
        }
    } );
    const ImGuiKey workspaceKeys[] = { ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4, ImGuiKey_5, ImGuiKey_6, ImGuiKey_7, ImGuiKey_8, ImGuiKey_9 };
    for( int workspace = 1; workspace <= 9; ++workspace )
    {
        const std::string id = "Workspace.Select" + std::to_string( workspace );
        const std::string name = "Workspace " + std::to_string( workspace ) + ( workspace <= 5 ? ": " + GetWorkspaceName( workspace ) : std::string() );
        actions.Register( { id, name, "Workspace", ImGuiMod_Shortcut | workspaceKeys[workspace - 1], 0, [this, workspace]() { SwitchWorkspace( workspace ); } } );
    }

    actions.Register( { "View.ResetLayout", "Reset Layout", "View", 0, 0, [this]() { ResetLayout(); } } );
    // ED-3: help and onboarding. Docs open in the OS's viewer (unattended runs only log them).
    const auto openDoc = []( const std::string& InDoc ) {
        CLog::Log( CLog::LogType::Info, "Help: " + InDoc );
        if( !AutomationRunner::IsUnattendedRun() )
        {
            PlatformUtils::OpenFile( Path( InDoc ) );
        }
    };
    actions.Register( { "Help.Documentation", "Documentation", "Help", ImGuiKey_F1, 0, [openDoc]() { openDoc( "Engine/Docs/README.md" ); } } );
    actions.Register( { "Help.GettingStarted", "Getting Started", "Help", 0, 0, [openDoc]() { openDoc( "Engine/Docs/Projects.md" ); } } );
    actions.Register( { "Help.Tour", "Take the Tour", "Help", 0, 0, [this]() { Tour->Start(); } } );
    actions.Register( { "Help.TourNext", "Tour: Next", "Help", 0, 0, [this]() { if( Tour->IsOpen ) Tour->Next(); } } );
    actions.Register( { "Help.TourBack", "Tour: Back", "Help", 0, 0, [this]() { if( Tour->IsOpen ) Tour->Back(); } } );
    actions.Register( { "Help.WhatsNew", "What's New", "Help", 0, 0, [this]() { WhatsNew->IsOpen = true; } } );

    InitUI();
}

Havana::~Havana()
{
    for( ImGuiMouseCursor imgui_cursor = 0; imgui_cursor < ImGuiMouseCursor_COUNT; imgui_cursor++ )
    {
        SDL_FreeCursor( g_imgui_to_sdl_cursor[imgui_cursor] );
    }
}

void Havana::InitUI()
{
    // Reset Layout reloads the dock settings between frames: ImGui rebuilds docking from settings
    // in NewFrame, and a tree replaced mid-frame trips its tab bar asserts.
    ImGuiContextHook layoutHook;
    layoutHook.Type = ImGuiContextHookType_NewFramePre;
    layoutHook.UserData = this;
    layoutHook.Callback = []( ImGuiContext*, ImGuiContextHook* InHook ) {
        Havana* self = static_cast<Havana*>( InHook->UserData );
        if( self->m_resetLayoutPending )
        {
            self->ApplyLayoutReset();
        }
    };
    ImGui::AddContextHook( ImGui::GetCurrentContext(), &layoutHook );
    // ME-272: after NewFrame's dock rebuild, hidden windows get their layout DockId back (they dock
    // there the next time they open).
    ImGuiContextHook restoreHook;
    restoreHook.Type = ImGuiContextHookType_NewFramePost;
    restoreHook.UserData = this;
    restoreHook.Callback = []( ImGuiContext*, ImGuiContextHook* InHook ) {
        Havana* self = static_cast<Havana*>( InHook->UserData );
        if( self->m_dockIdsToRestore.empty() || ImGui::GetCurrentContext()->DockContext.WantFullRebuild )
        {
            return;
        }
        for( const auto& [windowId, dockId] : self->m_dockIdsToRestore )
        {
            ImGuiWindow* window = ImGui::FindWindowByID( windowId );
            if( window && !window->DockNode && window->DockId == 0 )
            {
                window->DockId = dockId;
            }
        }
        self->m_dockIdsToRestore.clear();
    };
    ImGui::AddContextHook( ImGui::GetCurrentContext(), &restoreHook );

    // The dock layout is per user (.tmp/imgui.ini, never committed). The project's
    // Assets/Config/imgui.cfg is the default layout a fresh checkout starts from; it's never written.
    std::error_code ec;
    std::filesystem::create_directories( ".tmp", ec );
    EngineConfigFilePath = Path( ".tmp/imgui.ini", true );
    DefaultLayoutFilePath = Path( "Assets/Config/imgui.cfg" );
    ActiveWorkspace = EditorConfig::GetInstance().GetPreference<int>( "ActiveWorkspace", 1 );
    if( ActiveWorkspace < 1 || ActiveWorkspace > 9 ) ActiveWorkspace = 1;
    const Path activeWorkspaceLayout = GetWorkspaceLayoutPath( ActiveWorkspace );
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    if( activeWorkspaceLayout.Exists )
    {
        LoadLayoutFile( activeWorkspaceLayout.FullPath.c_str() );
    }
    else if( EngineConfigFilePath.Exists )
    {
        LoadLayoutFile( EngineConfigFilePath.FullPath.c_str() );
    }
    else if( DefaultLayoutFilePath.Exists )
    {
        LoadLayoutFile( DefaultLayoutFilePath.FullPath.c_str() );
    }
    // Unattended runs use the layout without ever writing it back.
    io.IniFilename = AutomationRunner::IsUnattendedRun() ? nullptr : EngineConfigFilePath.FullPath.c_str();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // ED-1: Tab and the arrows move through a focused window's widgets, Space / Enter press them.
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    //io.MouseDrawCursor = true;

    g_imgui_to_sdl_cursor[ImGuiMouseCursor_Arrow] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_ARROW );
    g_imgui_to_sdl_cursor[ImGuiMouseCursor_TextInput] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_IBEAM );
    g_imgui_to_sdl_cursor[ImGuiMouseCursor_Hand] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_HAND );
    g_imgui_to_sdl_cursor[ImGuiMouseCursor_ResizeNS] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_SIZENS );
    g_imgui_to_sdl_cursor[ImGuiMouseCursor_ResizeEW] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_SIZEWE );
    g_imgui_to_sdl_cursor[ImGuiMouseCursor_ResizeNESW] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_SIZENESW );
    g_imgui_to_sdl_cursor[ImGuiMouseCursor_ResizeNWSE] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_SIZENWSE );

    PreferencesWidget::ApplyAppearancePreferences();

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
    //MainMenu->Init();
    //LogPanel->Init();
    //ResourceMonitor->Init();
    //MainSceneView->Init();
    //GameSceneView->Init();
    //SceneHierarchy->Init();
    //PropertiesView->Init();
    //AssetPreview->Init();
    //AssetBrowser->Init();

    auto registry = GetWidgetRegistry();

    for( auto i : RegisteredWidgets )//EditorConfig::GetInstance().PanelVisibility)
    {
        i->Init();
        DefaultPanelOpen[i->Name] = i->IsOpen;
        auto& panelData = EditorConfig::GetInstance().PanelVisibility;
        if( panelData.find( i->Name ) != panelData.end() )
        {
            i->IsOpen = panelData[i->Name].IsVisible;
        }
        else
        {
            panelData[i->Name] = { i->IsOpen };
        }
    }

    for( auto it : registry )
    {
        auto customWidget = it.second.CreateFunc();
        CustomRegisteredWidgets.push_back( customWidget );
        customWidget->Init();
    }
    RestoreWorkspacePanelVisibility( ActiveWorkspace );
    // A user's first launch starts the tour (ED-3); it records when it's done or skipped.
    if( !AutomationRunner::IsUnattendedRun() && !EditorConfig::GetInstance().GetPreference<bool>( "Editor.TourDone", false ) )
    {
        Tour->Start();
    }

    ShaderGraphs->Init();
    Sequencer->InstallActionRouting();
    TerrainPanel->InstallActionRouting();
    Trees->Init();

    Input& gameInput = GetEngine().GetInput();
    gameInput.Stop();
}

void Havana::NewFrame()
{
    OPTICK_EVENT( "Havana::NewFrame" );

    Preferences->ApplyPendingAppearance();
    ImGuizmo::BeginFrame();

    EditorActions::Get().ProcessShortcuts();

    {
        OPTICK_EVENT( "MainMenu", Optick::Category::UI );
        MainMenu->SetData( &RegisteredWidgets, &CustomRegisteredWidgets, m_app );
        MainMenu->Update();
        MainMenu->Render();
    }

    // Dockspace
    {
        OPTICK_EVENT( "Dockspace Setup", Optick::Category::UI );
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        // BeginMainMenuBar reserves its actual height in the viewport work area. The gutter
        // around the dock space is as wide as the splitters between docked panels.
        const float dockMargin = ImGui::GetStyle().DockingSeparatorSize;
        DockPos = ImVec2( viewport->WorkPos.x + dockMargin, viewport->WorkPos.y + dockMargin );
        DockSize = ImVec2( std::max( 1.f, viewport->WorkSize.x - dockMargin * 2.f ),
            std::max( 1.f, viewport->WorkSize.y - dockMargin * 2.f ) );

        ImGui::SetNextWindowPos( DockPos );
        ImGui::SetNextWindowSize( DockSize );
        ImGui::SetNextWindowViewport( viewport->ID );
        ImGui::SetNextWindowBgAlpha( 0.0f );

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;
        window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 0.0f );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
        bool show_dockspace = true;
        ImGui::Begin( "MainDockSpace", &show_dockspace, window_flags );
        ImGui::PopStyleVar( 3 );
        ImGuiID dockspace_id = ImGui::GetID( "MyDockspace" );
        ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;
        {
            OPTICK_EVENT( "Dockspace Create", Optick::Category::UI );
            ImGui::DockSpace( dockspace_id, ImVec2( 0.0f, 0.0f ), dockspace_flags );

            ImGui::End();
        }
    }

    LogPanel->Render();
    AssetBrowser->Render();
    ResourceMonitor->Render();
    History->Render();
    ErrorList->Render();
    DrawToasts();
    LocalizationTables->Render();
    Tour->Render();
    WhatsNew->Render();
    UIDesigner->Render();
    Sequencer->Render();
    Profiler->Render();
    Preferences->Render();
    ProjectSettingsView->Render();
    BuildGameView->Render();
    OpenProjectView->Render();
    ShaderGraphs->Render();
    Trees->Render();
    TerrainPanel->Render();
    EditorActions::Get().DrawPalette();
}

void Havana::SetPlayMode( bool playing )
{
    GameSceneView->MaximizeOnPlay = playing && GameSceneView->MaximizeOnPlayPreference;
    if( playing )
    {
        ImGui::SetWindowFocus( GameSceneView->MaximizeOnPlay ? "Full Screen Viewport" : GameSceneView->Name.c_str() );
    }
}


void Havana::ShowWidget( const std::string& name )
{
    for( auto& widget : RegisteredWidgets )
    {
        if( widget->Name == name )
        {
            widget->IsOpen = true;
            ImGui::SetWindowFocus( name.c_str() );
        }
    }
}


int Havana::IsWidgetOpen( const std::string& name ) const
{
    for( const auto& widget : RegisteredWidgets )
    {
        if( widget->Name == name )
        {
            return widget->IsOpen ? 1 : 0;
        }
    }
    if( ShaderGraphs && ShaderGraphs->Name == name )
    {
        return ShaderGraphs->IsOpen ? 1 : 0;
    }
    return -1;
}


Path Havana::GetWorkspaceLayoutPath( int InWorkspace ) const
{
    return Path( ".tmp/imgui-workspace-" + std::to_string( InWorkspace ) + ".ini", true );
}


void Havana::StoreWorkspacePanelVisibility( int InWorkspace )
{
    json& workspaces = EditorConfig::GetInstance().Preferences["Workspaces"];
    if( !workspaces.is_object() ) workspaces = json::object();
    json& panels = workspaces[std::to_string( InWorkspace )]["Panels"] = json::object();
    for( const auto& widget : RegisteredWidgets )
    {
        if( widget.get() != MainMenu.get() ) panels[widget->Name] = widget->IsOpen;
    }
    for( const auto& widget : CustomRegisteredWidgets )
    {
        panels[widget->Name] = widget->IsOpen;
    }
}


std::string Havana::GetWorkspaceName( int InWorkspace )
{
    static const char* const kNames[] = { "Scene", "Animation", "Shader", "UI", "Debug" };
    return InWorkspace >= 1 && InWorkspace <= 5 ? kNames[InWorkspace - 1] : "Workspace " + std::to_string( InWorkspace );
}


namespace
{
    // ME-272: a layout can dock a window into a node that has since been split (Assets/Config/
    // imgui.cfg docks Debug, Game Preview and Resource Monitor into split node 0x9). ImGui gives
    // that split node a tab bar once such a window shows, and DockNodeUpdate asserts. Each such
    // window goes to the split node's first leaf as the layout loads.
    std::string SanitizeLayout( const std::string& InIni )
    {
        std::vector<std::string> lines;
        {
            std::istringstream stream( InIni );
            for( std::string line; std::getline( stream, line ); )
            {
                lines.push_back( line );
            }
        }
        auto field = []( const std::string& InLine, const char* InKey ) {
            const size_t at = InLine.find( InKey );
            if( at == std::string::npos )
            {
                return std::string();
            }
            const size_t begin = at + std::strlen( InKey );
            return InLine.substr( begin, InLine.find_first_of( " ,\r", begin ) - begin );
        };
        std::map<std::string, std::vector<std::string>> children;     // node -> children in file order
        std::set<std::string> splitNodes;
        for( const std::string& line : lines )
        {
            const size_t first = line.find_first_not_of( " \t" );
            if( first == std::string::npos || ( line.compare( first, 8, "DockNode" ) != 0 && line.compare( first, 9, "DockSpace" ) != 0 ) )
            {
                continue;
            }
            const std::string id = field( line, " ID=" );
            const std::string parent = field( line, " Parent=" );
            if( !parent.empty() )
            {
                children[parent].push_back( id );
            }
            if( line.find( " Split=" ) != std::string::npos )
            {
                splitNodes.insert( id );
            }
        }
        std::string out;
        out.reserve( InIni.size() );
        for( std::string& line : lines )
        {
            if( line.rfind( "DockId=", 0 ) == 0 )
            {
                const std::string id = field( line, "DockId=" );
                if( splitNodes.count( id ) )
                {
                    std::string leaf = id;
                    while( splitNodes.count( leaf ) && !children[leaf].empty() )
                    {
                        leaf = children[leaf].front();
                    }
                    line.replace( 7, id.size(), leaf );
                }
            }
            out += line;
            out += '\n';
        }
        return out;
    }
}


void Havana::LoadLayoutFile( const char* InPath )
{
    std::ifstream file( InPath, std::ios::binary );
    if( !file )
    {
        return;
    }
    std::stringstream text;
    text << file.rdbuf();
    const std::string ini = SanitizeLayout( text.str() );
    ImGui::LoadIniSettingsFromMemory( ini.c_str(), ini.size() );
}


void Havana::DrawToasts()
{
    // ED-14: notices from background work (a failed script build...), bottom right for a few
    // seconds, with the action that fixes or shows it.
    const double now = ImGui::GetTime();
    for( WorkStatus::Toast& toast : WorkStatus::Get().TakeToasts() )
    {
        m_toasts.push_back( { std::move( toast ), now } );
    }
    m_toasts.erase( std::remove_if( m_toasts.begin(), m_toasts.end(), [now]( const ShownToast& InToast ) { return now - InToast.ShownAt > 8.0; } ), m_toasts.end() );
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    float bottom = viewport->Pos.y + viewport->Size.y - ImGuiTheme::Em( 1.f );
    for( size_t i = 0; i < m_toasts.size(); ++i )
    {
        const WorkStatus::Toast& toast = m_toasts[i].Notice;
        ImGui::SetNextWindowPos( ImVec2( viewport->Pos.x + viewport->Size.x - ImGuiTheme::Em( 1.f ), bottom ), ImGuiCond_Always, ImVec2( 1.f, 1.f ) );
        ImGui::SetNextWindowBgAlpha( 0.95f );
        const std::string id = "##Toast" + std::to_string( i );
        if( ImGui::Begin( id.c_str(), nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking ) )
        {
            const ImVec4 color = toast.Severity == WorkStatus::Level::Error ? ImGuiTheme::ErrorText()
                : toast.Severity == WorkStatus::Level::Warning ? ImGuiTheme::WarningText() : ImGui::GetStyleColorVec4( ImGuiCol_Text );
            ImGui::TextColored( color, "%s", toast.Message.c_str() );
            if( !toast.ActionId.empty() )
            {
                ImGui::SameLine();
                if( ImGui::SmallButton( ( ( toast.ActionLabel.empty() ? std::string( "Fix" ) : toast.ActionLabel ) + id ).c_str() ) )
                {
                    EditorActions::Get().Execute( toast.ActionId );
                    m_toasts[i].ShownAt = -100.0;
                }
            }
            bottom -= ImGui::GetWindowHeight() + ImGuiTheme::Em( 0.4f );
        }
        ImGui::End();
    }
}


void Havana::RequestDockRebuild()
{
    ImGuiContext& context = *ImGui::GetCurrentContext();
    for( ImGuiWindow* window : context.Windows )
    {
        if( window->DockId == 0 || window->DockNode )
        {
            continue;
        }
        // Where the freshly loaded layout puts it (the rebuild re-saves live state over it).
        const ImGuiWindowSettings* settings = ImGui::FindWindowSettingsByID( window->ID );
        const ImGuiID dockId = settings && settings->DockId ? settings->DockId : window->DockId;
        m_dockIdsToRestore.emplace_back( window->ID, dockId );
        window->DockId = 0;
    }
    context.DockContext.WantFullRebuild = true;
}


void Havana::ResetLayout()
{
    m_resetLayoutPending = true;
}


void Havana::ApplyLayoutReset()
{
    // Two frames: first the panels go back to their startup visibility (closing a docked window and
    // replacing the dock tree in one frame trips ImGui's tab bar asserts), then the layout reloads
    // and the workspace's own panels open.
    if( m_resetLayoutPhase == 0 )
    {
        for( const auto& widget : RegisteredWidgets )
        {
            const auto open = DefaultPanelOpen.find( widget->Name );
            if( widget.get() != MainMenu.get() && open != DefaultPanelOpen.end() )
            {
                widget->IsOpen = open->second;
            }
        }
        if( ShaderGraphs )
        {
            ShaderGraphs->IsOpen = false;
        }
        m_resetLayoutPhase = 1;
        return;
    }
    m_resetLayoutPhase = 0;
    m_resetLayoutPending = false;
    ImGui::ClearIniSettings();
    if( DefaultLayoutFilePath.Exists )
    {
        LoadLayoutFile( DefaultLayoutFilePath.FullPath.c_str() );
    }
    // NewFrame rebuilds the dock tree from the loaded settings (patching live nodes in place
    // leaves the windows docked in them with stale tab bars).
    RequestDockRebuild();
    std::error_code error;
    std::filesystem::remove( GetWorkspaceLayoutPath( ActiveWorkspace ).FullPath, error );
    json& workspaces = EditorConfig::GetInstance().Preferences["Workspaces"];
    if( workspaces.is_object() )
    {
        workspaces.erase( std::to_string( ActiveWorkspace ) );
    }
    // The workspace's own panels on top of the defaults (Animation opens the Sequencer...).
    RestoreWorkspacePanelVisibility( ActiveWorkspace );
    if( !AutomationRunner::IsUnattendedRun() )
    {
        ImGui::SaveIniSettingsToDisk( EngineConfigFilePath.FullPath.c_str() );
        EditorConfig::GetInstance().Save();
    }
    CLog::Log( CLog::LogType::Info, "Layout: reset workspace " + std::to_string( ActiveWorkspace ) + " (" + GetWorkspaceName( ActiveWorkspace ) + ")" );
}


void Havana::RestoreWorkspacePanelVisibility( int InWorkspace )
{
    // A named workspace nobody has arranged yet opens its subject's panels.
    static const std::map<int, std::vector<std::string>> kNamedPanels = {
        { 2, { "Sequencer", "Preview" } },
        { 3, { "Shader Graph", "Preview" } },
        { 4, { "Localization", "Preview" } },
        { 5, { "Profiler", "Resource Monitor", "Log" } },
    };
    const json& preferences = EditorConfig::GetInstance().Preferences;
    const auto workspace = preferences.find( "Workspaces" );
    const bool stored = workspace != preferences.end() && workspace->is_object() && workspace->contains( std::to_string( InWorkspace ) );
    if( !stored )
    {
        const auto named = kNamedPanels.find( InWorkspace );
        const auto openIfNamed = [&named]( HavanaWidget& InWidget ) {
            if( named != kNamedPanels.end() && std::find( named->second.begin(), named->second.end(), InWidget.Name ) != named->second.end() )
            {
                InWidget.IsOpen = true;
            }
        };
        for( const auto& widget : RegisteredWidgets )
        {
            openIfNamed( *widget );
        }
        if( ShaderGraphs )
        {
            openIfNamed( *ShaderGraphs );
        }
        return;
    }
    const auto slot = workspace->find( std::to_string( InWorkspace ) );
    if( slot == workspace->end() || !slot->is_object() ) return;
    const auto panels = slot->find( "Panels" );
    if( panels == slot->end() || !panels->is_object() ) return;
    auto restore = [&panels]( const SharedPtr<HavanaWidget>& widget ) {
        const auto visible = panels->find( widget->Name );
        if( visible != panels->end() && visible->is_boolean() ) widget->IsOpen = visible->get<bool>();
    };
    for( const auto& widget : RegisteredWidgets )
    {
        if( widget.get() != MainMenu.get() ) restore( widget );
    }
    for( const auto& widget : CustomRegisteredWidgets ) restore( widget );
}


void Havana::SaveWorkspaceLayout( int InWorkspace )
{
    if( AutomationRunner::IsUnattendedRun() ) return;
    ImGui::SaveIniSettingsToDisk( GetWorkspaceLayoutPath( InWorkspace ).FullPath.c_str() );
    StoreWorkspacePanelVisibility( InWorkspace );
    EditorConfig::GetInstance().Save();
}


void Havana::SwitchWorkspace( int InWorkspace )
{
    if( InWorkspace < 1 || InWorkspace > 9 || InWorkspace == ActiveWorkspace ) return;

    SaveWorkspaceLayout( ActiveWorkspace );
    const Path target = GetWorkspaceLayoutPath( InWorkspace );
    ImGui::ClearIniSettings();
    if( target.Exists ) LoadLayoutFile( target.FullPath.c_str() );
    else if( DefaultLayoutFilePath.Exists ) LoadLayoutFile( DefaultLayoutFilePath.FullPath.c_str() );
    // A full dock rebuild next frame: with panels open and docked, patching the live tree trips
    // ImGui's tab bar asserts (as Reset Layout did).
    RequestDockRebuild();
    RestoreWorkspacePanelVisibility( InWorkspace );
    ActiveWorkspace = InWorkspace;
    EditorConfig::GetInstance().SetPreference( "ActiveWorkspace", ActiveWorkspace );

    // Keep the normal startup layout in sync with the selected workspace.
    if( !AutomationRunner::IsUnattendedRun() )
    {
        ImGui::SaveIniSettingsToDisk( EngineConfigFilePath.FullPath.c_str() );
        EditorConfig::GetInstance().Save();
    }
}


bool Havana::IsSceneContextFocused() const
{
    ImGuiContext* context = ImGui::GetCurrentContext();
    ImGuiWindow* focused = context ? context->NavWindow : nullptr;
    if( !focused )
    {
        // Nothing focused (e.g. right after a click on empty dock space): treat as the scene.
        return true;
    }
    const char* name = focused->RootWindow ? focused->RootWindow->Name : focused->Name;
    return std::strcmp( name, "Hierarchy" ) == 0 || std::strcmp( name, "World View" ) == 0;
}

void Havana::UpdateWorld( Transform* root, std::vector<Entity>& ents )
{
    SceneHierarchy->SetData( root, ents );
    SceneHierarchy->Render();

    PropertiesView->Render();
    AssetPreview->Render();
}

Input& Havana::GetInput()
{
    return m_engine->GetEditorInput();
}

// While the game owns input its cursor stays inside the Game View image (hidden and captured
// with relative motion) and ImGui ignores the mouse, so neither a stray nor an
// invisible cursor can click editor panels. Escape hands input back to the editor and frees it.
void Havana::UpdateGameCursor()
{
    Input& gameInput = GetEngine().GetInput();
    SDL_Window* window = nullptr;
    Vector2 min;
    Vector2 size;
    if( GameSceneView && GameSceneView->GetImageWindowRect( window, min, size ) )
    {
        gameInput.SetCursorRegion( window, min, size );
    }
    else
    {
        gameInput.ClearCursorRegion();
    }

    const bool block = m_app->IsGameRunning() && gameInput.IsCapturing();
    if( block != m_imguiMouseBlocked )
    {
        ImGuiIO& io = ImGui::GetIO();
        constexpr ImGuiConfigFlags kFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange;
        io.ConfigFlags = block ? ( io.ConfigFlags | kFlags ) : ( io.ConfigFlags & ~kFlags );
        m_imguiMouseBlocked = block;
    }
}


void Havana::Render( Moonlight::CameraData& EditorCamera )
{
    OPTICK_EVENT( "Havana::Render", Optick::Category::UI );

    // Editor Scene View
    {
        OPTICK_EVENT( "MainSceneView", Optick::Category::UI );
        MainSceneView->SetData( EditorCamera );
        MainSceneView->Render();

        // GPU picking for clicks (not drags) on the scene view, in viewport pixels.
        FrameRenderData& frameRenderData = GetEngine().m_frameRenderSettings;
        frameRenderData.MousePosition = MainSceneView->ViewportMousePosition;
        frameRenderData.WasLeftPressed = MainSceneView->ConsumeClick();
    }

    // Game View
    {
        OPTICK_EVENT( "GameSceneView", Optick::Category::UI );
        // The current scene camera. One outside the scene (the editor's own while the scene has no
        // camera, or a deactivated one) has no cache entry: the Game View then shows nothing.
        BGFXRenderer& renderer = GetEngine().GetRenderer();
        Moonlight::CameraData* camData = Camera::CurrentCamera ? renderer.GetCameraCache().Get( Camera::CurrentCamera->GetCameraId() ) : nullptr;
        if( camData && !camData->IsMain )
        {
            camData = nullptr;
        }
        if( camData )
        {
            GameSceneView->SetData( *camData );
        }
        else
        {
            GameSceneView->ClearData();
        }
        GameSceneView->Render();

        if( m_app->IsGameRunning() )
        {
            Input& gameInput = GetEngine().GetInput();
            if( gameInput.IsKeyDown( KeyCode::Escape ) )
            {
                gameInput.Stop();
                ImGui::SetWindowFocus( "Hierarchy" );
                GetInput().Resume();
            }
            else if( GameSceneView->IsFocused && GetInput().IsMouseButtonDown( MouseButton::Left ) )
            {
                GetInput().Stop();
                gameInput.Resume();
            }
        }
        UpdateGameCursor();

        // #TODO: Remove this
        // Update: This crashes macOS, but is needed for to display the game as the correct size in the editor
        Vector2 gameOutputSize = GetGameOutputSize();
        if( Camera::CurrentCamera ) Camera::CurrentCamera->OutputSize = gameOutputSize;
        if( camData && camData->Buffer && ((int)gameOutputSize.x != (int)camData->Buffer->GetOutputWidth() || (int)gameOutputSize.y != (int)camData->Buffer->GetOutputHeight() ) )
        {
            camData->Buffer->Resize( gameOutputSize );
        }
    }


#if USING( ME_BASIC_PROFILER )
    // Frame Profiler
    {
        OPTICK_EVENT( "Frame Profiler", Optick::Category::UI );
        Vector2 size( ImGui::GetMainViewport()->Size.x - 12.f, static_cast<float>( FrameProfile::kMinProfilerSize ) );
        auto pos = ImGui::GetMainViewport()->Pos;
        Vector2 position( pos.x + 6.f, pos.y + ImGui::GetMainViewport()->Size.y - ( static_cast<float>( FrameProfile::kMinProfilerSize ) * 2.f ) );

        FrameProfile::GetInstance().Render( position, size );
    }
#endif

    // Custom User Widgets
    {
        OPTICK_CATEGORY( "Custom User Widgets", Optick::Category::UI );
        for( auto customWidget : CustomRegisteredWidgets )
        {
            if( customWidget->IsOpen )
            {
                customWidget->Render();
            }
        }
    }

    //SDL_Cursor* cursor = g_imgui_to_sdl_cursor[ImGui::GetMouseCursor()];
    if( !IsGameFocused() )
    {
        SDL_SetCursor( g_imgui_to_sdl_cursor[ImGui::GetMouseCursor()] );
    }
}

void Havana::SetWindowTitle( const std::string& title )
{
    MainMenu->SetWindowTitle( title );
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

void Havana::Save()
{
    // The active slot may be the last one used this session, so save it even when
    // no workspace switch happened before the editor closed.
    SaveWorkspaceLayout( ActiveWorkspace );
    for( auto i : RegisteredWidgets )
    {
        auto& panelData = EditorConfig::GetInstance().PanelVisibility;

        panelData[i->Name] = { i->IsOpen };
    }
}

void Havana::Destroy()
{
    Trees->Destroy();
    ShaderGraphs->Destroy();
    for( const auto& widget : RegisteredWidgets )
    {
        widget->Destroy();
    }
    EditorIcons::Shutdown();
}

#endif
