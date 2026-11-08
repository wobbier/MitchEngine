#include "Havana.h"
#include <algorithm>
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
#include "Editor/Inspectors/LightInspector.h"
#include "Editor/Inspectors/CameraInspector.h"
#include "Editor/Inspectors/PostProcessInspector.h"
#include "Editor/Inspectors/PhysicsInspector.h"
#include "Editor/Inspectors/AudioInspector.h"
#include "Editor/Inspectors/CharacterInspector.h"
#include "Editor/Inspectors/TransformInspector.h"
#include "Window/IWindow.h"
#include "Widgets/HistoryWidget.h"
#include "Widgets/WidgetGalleryWidget.h"
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
#include "Widgets/NetDebuggerWidget.h"
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
    Gallery.reset( new WidgetGalleryWidget() );
    RegisteredWidgets.push_back( Gallery );
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

    NetDebugger.reset( new NetDebuggerWidget( app ) );
    RegisteredWidgets.push_back( NetDebugger );

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
    // ED-9: builds the profile selected in Build Game, then runs it from its build folder.
    actions.Register( { "File.BuildAndRun", "Build & Run", "File", 0, 0, [this]() {
        ShowWidget( "Build Game" );
        if( BuildGameWidget* widget = BuildGameWidget::Get() )
        {
            widget->StartBuild( true );
        }
    } } );
    actions.Register( { "File.OpenProject", "Open Project...", "File", 0, 0, [this]() { ShowWidget( "Open Project" ); } } );
    actions.Register( { "File.ProjectHub", "Project Hub...", "File", 0, 0, [app]() { app->RequestProjectHub(); } } );
    actions.Register( { "View.Profiler", "Profiler", "View", 0, 0, [this]() { ShowWidget( "Profiler" ); } } );
    actions.Register( { "View.NetDebugger", "Net Debugger", "View", 0, 0, [this]() { ShowWidget( "Net Debugger" ); } } );
    actions.Register( { "View.ErrorList", "Error List", "View", 0, 0, [this]() { ShowWidget( "Error List" ); } } );
    ReflectionUI::RegisterDefaultDrawers();
    LightInspector::Register();
    CameraInspector::Register();
    PostProcessInspector::Register();
    PhysicsInspector::Register();
    AudioInspector::Register();
    CharacterInspector::Register();
    TransformInspector::Register();
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
    actions.SearchProviders.push_back( [this]( std::vector<EditorActions::PaletteItem>& OutItems ) {
        for( const Workspace& workspace : Workspaces )
        {
            const int id = workspace.Id;
            OutItems.push_back( { "Workspace", workspace.Name, [this, id]() { SwitchWorkspace( id ); } } );
        }
    } );

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
        // A workspace switch loads its layout between frames: loaded mid-frame, windows that haven't
        // drawn yet that frame come undocked.
        if( self->m_pendingWorkspaceDock )
        {
            const Workspace* workspace = self->FindWorkspace( self->m_pendingWorkspaceDock );
            self->m_pendingWorkspaceDock = 0;
            if( workspace ) self->LoadWorkspaceDock( *workspace );
        }
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
    // Unattended runs (editor flows, screenshots) start from the project's default layout and
    // panels in workspace 1, like a fresh checkout: a user's own arrangement (a short Game View, a
    // panel floating over the World View) moves or covers what the flows click (ME-307).
    const bool userLayout = !AutomationRunner::IsUnattendedRun();
    LoadWorkspaceList( userLayout );
    ActiveWorkspace = userLayout ? EditorConfig::GetInstance().GetPreference<int>( "ActiveWorkspace", Workspaces.front().Id ) : Workspaces.front().Id;
    if( !FindWorkspace( ActiveWorkspace ) ) ActiveWorkspace = Workspaces.front().Id;
    RegisterWorkspaceActions();
    const Workspace& activeWorkspace = *FindWorkspace( ActiveWorkspace );
    const Path activeWorkspaceLayout = GetWorkspaceLayoutPath( ActiveWorkspace );
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    if( userLayout && activeWorkspaceLayout.Exists )
    {
        LoadLayoutFile( activeWorkspaceLayout.FullPath.c_str() );
    }
    else if( userLayout && activeWorkspace.Template != "Scene" )
    {
        // Built in code before the first frame's DockSpace; nothing loads .tmp/imgui.ini over it.
        ImGui::LoadIniSettingsFromMemory( "" );
        m_pendingDockTemplate = activeWorkspace.Template;
    }
    else if( userLayout && EngineConfigFilePath.Exists )
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
        if( userLayout && panelData.find( i->Name ) != panelData.end() )
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
    if( userLayout )
    {
        RestoreWorkspacePanelVisibility( ActiveWorkspace );
    }
    else
    {
        // Its output is in the run's own log, and the default layout floats it over the World View
        // where it would take the flows' clicks and drags.
        LogPanel->IsOpen = false;
    }
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
        // Maximize On Play covers the dock with the Game View: keep the layout but hide every
        // panel docked in it, so none of them draws (or, the World View, renders its camera)
        // unseen behind the game. Panels floating in their own windows stay.
        if( GameSceneView && GameSceneView->MaximizeOnPlay )
        {
            dockspace_flags |= ImGuiDockNodeFlags_KeepAliveOnly;
        }
        {
            OPTICK_EVENT( "Dockspace Create", Optick::Category::UI );
            // A built layout's first tabs, once their windows have drawn into it (a tab bar picks
            // the most recently shown window otherwise).
            for( auto it = m_pendingTabSelections.begin(); it != m_pendingTabSelections.end(); )
            {
                ImGuiWindow* window = ImGui::FindWindowByName( it->c_str() );
                if( window && window->DockNode && window->DockNode->TabBar )
                {
                    window->DockNode->TabBar->NextSelectedTabId = window->TabId;
                    it = m_pendingTabSelections.erase( it );
                }
                else ++it;
            }
            if( !m_pendingDockTemplate.empty() )
            {
                BuildTemplateDock( m_pendingDockTemplate, dockspace_id );
                m_pendingDockTemplate.clear();
            }
            ImGui::DockSpace( dockspace_id, ImVec2( 0.0f, 0.0f ), dockspace_flags );

            ImGui::End();
        }
    }

    LogPanel->Render();
    AssetBrowser->Render();
    ResourceMonitor->Render();
    History->Render();
    Gallery->Render();
    ErrorList->Render();
    DrawToasts();
    LocalizationTables->Render();
    Tour->Render();
    WhatsNew->Render();
    UIDesigner->Render();
    Sequencer->Render();
    Profiler->Render();
    NetDebugger->Render();
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


namespace
{
    // Panels a workspace template arranges. The rest (Preferences, Build Game, the tour...) are
    // dialogs: a workspace switch leaves them as they are.
    const std::set<std::string> kLayoutPanels = {
        "Asset Browser", "Log", "Resource Monitor", "World View", "Game View", "Hierarchy", "Properties",
        "Preview", "History", "Widget Gallery", "Error List", "Localization", "UI Designer", "Sequencer",
        "Profiler", "Net Debugger", "Terrain", "Shader Graph",
    };

    // Each template's panels (widget names). Scene is the shipped layout and each panel's startup visibility.
    const std::map<std::string, std::vector<std::string>> kTemplatePanels = {
        { "Animation", { "World View", "Game View", "Hierarchy", "Properties", "Preview", "Sequencer" } },
        { "Shader", { "World View", "Preview", "Properties", "Asset Browser", "Log", "Shader Graph" } },
        { "UI", { "Game View", "UI Designer", "Localization", "Asset Browser", "Log" } },
        { "Debug", { "Game View", "World View", "Hierarchy", "Properties", "Log", "Error List", "Profiler", "Resource Monitor" } },
        { "Network", { "Game View", "World View", "Net Debugger", "Profiler", "Log" } },
    };

    // A panel's window title, where it isn't the widget's name.
    std::string PanelWindowTitle( const std::string& InPanel )
    {
        return InPanel == "Asset Browser" ? "Asset Directory" : InPanel;
    }
}


const std::vector<std::string>& Havana::GetWorkspaceTemplates()
{
    static const std::vector<std::string> kTemplates = { "Scene", "Animation", "Shader", "UI", "Debug", "Network" };
    return kTemplates;
}


std::vector<Havana::Workspace> Havana::DefaultWorkspaces()
{
    // Ids 1-5 are the old fixed slots (their saved layouts carry over); Network came later.
    return {
        { 1, "Scene", "Scene" },
        { 2, "Animation", "Animation" },
        { 3, "Shader", "Shader" },
        { 4, "UI", "UI" },
        { 5, "Debug", "Debug" },
        { 10, "Network", "Network" },
    };
}


const Havana::Workspace* Havana::FindWorkspace( int InId ) const
{
    for( const Workspace& workspace : Workspaces )
    {
        if( workspace.Id == InId ) return &workspace;
    }
    return nullptr;
}


int Havana::GetWorkspaceIndex( int InId ) const
{
    for( size_t i = 0; i < Workspaces.size(); ++i )
    {
        if( Workspaces[i].Id == InId ) return static_cast<int>( i );
    }
    return -1;
}


std::string Havana::GetWorkspaceName( int InId ) const
{
    const Workspace* workspace = FindWorkspace( InId );
    return workspace ? workspace->Name : "Workspace " + std::to_string( InId );
}


void Havana::LoadWorkspaceList( bool InUserLayout )
{
    Workspaces.clear();
    // Unattended runs (editor flows) always get the default set, in order.
    if( !InUserLayout )
    {
        Workspaces = DefaultWorkspaces();
        return;
    }
    const std::vector<std::string>& templates = GetWorkspaceTemplates();
    json& preferences = EditorConfig::GetInstance().Preferences;
    const auto list = preferences.find( "WorkspaceList" );
    if( list != preferences.end() && list->is_array() )
    {
        for( const json& entry : *list )
        {
            if( !entry.is_object() ) continue;
            Workspace workspace;
            try
            {
                workspace.Id = entry.value( "Id", 0 );
                workspace.Name = entry.value( "Name", std::string() );
                workspace.Template = entry.value( "Template", std::string( "Scene" ) );
            }
            catch( ... )
            {
                continue;     // a hand-edited entry of the wrong type
            }
            if( workspace.Id <= 0 || FindWorkspace( workspace.Id ) ) continue;
            if( std::find( templates.begin(), templates.end(), workspace.Template ) == templates.end() ) workspace.Template = "Scene";
            if( workspace.Name.empty() ) workspace.Name = "Workspace " + std::to_string( workspace.Id );
            Workspaces.push_back( workspace );
        }
        if( !Workspaces.empty() ) return;
    }

    // First run with named workspaces: the old slots 2-5 only ever saved the shipped layout with a
    // few panels opened (every switch saved them), so they start from their new templates; their files
    // are kept beside as *.pre-templates.ini. Slot 1 keeps its layout; slots 6-9 that hold one stay.
    Workspaces = DefaultWorkspaces();
    json& stored = preferences["Workspaces"];
    if( !stored.is_object() ) stored = json::object();
    std::error_code error;
    for( int slot = 2; slot <= 5; ++slot )
    {
        const Path layout = GetWorkspaceLayoutPath( slot );
        if( layout.Exists )
        {
            std::filesystem::rename( layout.FullPath, std::filesystem::path( layout.FullPath ).replace_extension( ".pre-templates.ini" ), error );
        }
        stored.erase( std::to_string( slot ) );
    }
    for( int slot = 6; slot <= 9; ++slot )
    {
        if( GetWorkspaceLayoutPath( slot ).Exists || stored.contains( std::to_string( slot ) ) )
        {
            Workspaces.push_back( { slot, "Workspace " + std::to_string( slot ), "Scene" } );
        }
    }
    SaveWorkspaceList();
}


void Havana::SaveWorkspaceList()
{
    if( AutomationRunner::IsUnattendedRun() ) return;
    json list = json::array();
    for( const Workspace& workspace : Workspaces )
    {
        list.push_back( { { "Id", workspace.Id }, { "Name", workspace.Name }, { "Template", workspace.Template } } );
    }
    EditorConfig::GetInstance().Preferences["WorkspaceList"] = list;
    EditorConfig::GetInstance().Save();
}


void Havana::RegisterWorkspaceActions()
{
    EditorActions& actions = EditorActions::Get();
    const ImGuiKey workspaceKeys[] = { ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4, ImGuiKey_5, ImGuiKey_6, ImGuiKey_7, ImGuiKey_8, ImGuiKey_9 };
    for( int index = 0; index < 9; ++index )
    {
        // By position in the tab order, so a reorder moves the shortcut with the tab.
        const std::string id = "Workspace.Select" + std::to_string( index + 1 );
        const bool exists = index < static_cast<int>( Workspaces.size() );
        const std::string name = "Workspace " + std::to_string( index + 1 ) + ( exists ? ": " + Workspaces[index].Name : std::string() );
        EditorAction action{ id, name, "Workspace", ImGuiMod_Shortcut | workspaceKeys[index], 0, [this, index]() {
            if( index < static_cast<int>( Workspaces.size() ) ) SwitchWorkspace( Workspaces[index].Id );
        } };
        action.IsEnabled = [this, index]() { return index < static_cast<int>( Workspaces.size() ); };
        action.HideInPalette = !exists;
        actions.Register( action );
    }
    actions.Register( { "Workspace.New", "New Workspace (Duplicate Current)", "Workspace", 0, 0, [this]() { NewWorkspace( "" ); } } );
    actions.Register( { "Workspace.Rename", "Rename Workspace", "Workspace", 0, 0, [this]() { RenamingWorkspace = ActiveWorkspace; } } );
    actions.Register( { "Workspace.MoveLeft", "Move Workspace Left", "Workspace", 0, 0, [this]() { MoveWorkspace( ActiveWorkspace, GetWorkspaceIndex( ActiveWorkspace ) - 1 ); } } );
    actions.Register( { "Workspace.MoveRight", "Move Workspace Right", "Workspace", 0, 0, [this]() { MoveWorkspace( ActiveWorkspace, GetWorkspaceIndex( ActiveWorkspace ) + 1 ); } } );
    EditorAction remove{ "Workspace.Delete", "Delete Workspace", "Workspace", 0, 0, [this]() { DeleteWorkspace( ActiveWorkspace ); } };
    remove.IsEnabled = [this]() { return Workspaces.size() > 1; };
    actions.Register( remove );
}


int Havana::NewWorkspace( const std::string& InTemplate )
{
    const Workspace* source = FindWorkspace( ActiveWorkspace );
    const std::vector<std::string>& templates = GetWorkspaceTemplates();
    if( !InTemplate.empty() && std::find( templates.begin(), templates.end(), InTemplate ) == templates.end() ) return 0;

    Workspace workspace;
    for( const Workspace& existing : Workspaces ) workspace.Id = std::max( workspace.Id, existing.Id );
    ++workspace.Id;
    workspace.Template = InTemplate.empty() ? ( source ? source->Template : std::string( "Scene" ) ) : InTemplate;
    // "Animation", then "Animation 2"...
    const std::string base = InTemplate.empty() && source ? source->Name : workspace.Template;
    workspace.Name = base;
    for( int n = 2; std::any_of( Workspaces.begin(), Workspaces.end(), [&workspace]( const Workspace& w ) { return w.Name == workspace.Name; } ); ++n )
    {
        workspace.Name = base + " " + std::to_string( n );
    }

    if( InTemplate.empty() && source && !AutomationRunner::IsUnattendedRun() )
    {
        // A copy of the active workspace as it is now: its layout and its open panels.
        SaveWorkspaceLayout( ActiveWorkspace );
        std::error_code error;
        std::filesystem::copy_file( GetWorkspaceLayoutPath( ActiveWorkspace ).FullPath, GetWorkspaceLayoutPath( workspace.Id ).FullPath,
            std::filesystem::copy_options::overwrite_existing, error );
        json& stored = EditorConfig::GetInstance().Preferences["Workspaces"];
        if( stored.is_object() && stored.contains( std::to_string( ActiveWorkspace ) ) )
        {
            stored[std::to_string( workspace.Id )] = stored[std::to_string( ActiveWorkspace )];
        }
    }
    const int at = source ? GetWorkspaceIndex( source->Id ) + 1 : static_cast<int>( Workspaces.size() );
    Workspaces.insert( Workspaces.begin() + at, workspace );
    SaveWorkspaceList();
    RegisterWorkspaceActions();
    SwitchWorkspace( workspace.Id );
    return workspace.Id;
}


void Havana::RenameWorkspace( int InId, const std::string& InName )
{
    const size_t first = InName.find_first_not_of( " \t" );
    if( first == std::string::npos ) return;
    const std::string name = InName.substr( first, InName.find_last_not_of( " \t" ) - first + 1 );
    const int index = GetWorkspaceIndex( InId );
    if( index < 0 || Workspaces[index].Name == name ) return;
    Workspaces[index].Name = name;
    SaveWorkspaceList();
    RegisterWorkspaceActions();
}


void Havana::MoveWorkspace( int InId, int InIndex )
{
    const int from = GetWorkspaceIndex( InId );
    const int to = std::clamp( InIndex, 0, static_cast<int>( Workspaces.size() ) - 1 );
    if( from < 0 || from == to ) return;
    const Workspace moved = Workspaces[from];
    Workspaces.erase( Workspaces.begin() + from );
    Workspaces.insert( Workspaces.begin() + to, moved );
    SaveWorkspaceList();
    RegisterWorkspaceActions();
}


void Havana::DeleteWorkspace( int InId )
{
    const int index = GetWorkspaceIndex( InId );
    if( index < 0 || Workspaces.size() <= 1 ) return;
    if( InId == ActiveWorkspace )
    {
        SwitchWorkspace( Workspaces[index + 1 < static_cast<int>( Workspaces.size() ) ? index + 1 : index - 1].Id );
    }
    std::error_code error;
    std::filesystem::remove( GetWorkspaceLayoutPath( InId ).FullPath, error );
    json& stored = EditorConfig::GetInstance().Preferences["Workspaces"];
    if( stored.is_object() ) stored.erase( std::to_string( InId ) );
    if( RenamingWorkspace == InId ) RenamingWorkspace = 0;
    CLog::Log( CLog::LogType::Info, "Layout: deleted workspace " + Workspaces[GetWorkspaceIndex( InId )].Name );
    Workspaces.erase( Workspaces.begin() + GetWorkspaceIndex( InId ) );
    SaveWorkspaceList();
    RegisterWorkspaceActions();
}


void Havana::ApplyTemplatePanels( const std::string& InTemplate )
{
    const auto panels = kTemplatePanels.find( InTemplate );
    if( panels == kTemplatePanels.end() )
    {
        // Scene: every panel's startup visibility.
        for( const auto& widget : RegisteredWidgets )
        {
            const auto open = DefaultPanelOpen.find( widget->Name );
            if( widget.get() != MainMenu.get() && open != DefaultPanelOpen.end() )
            {
                widget->IsOpen = open->second;
            }
        }
        if( ShaderGraphs ) ShaderGraphs->IsOpen = false;
        return;
    }
    const auto apply = [&panels]( HavanaWidget& InWidget ) {
        if( kLayoutPanels.count( InWidget.Name ) )
        {
            InWidget.IsOpen = std::find( panels->second.begin(), panels->second.end(), InWidget.Name ) != panels->second.end();
        }
    };
    for( const auto& widget : RegisteredWidgets )
    {
        if( widget.get() != MainMenu.get() ) apply( *widget );
    }
    if( ShaderGraphs ) apply( *ShaderGraphs );
}


bool Havana::LoadWorkspaceDock( const Workspace& InWorkspace )
{
    // Open documents (shader graphs, trees) docked now go to the new layout's middle.
    m_pendingDockDocuments.clear();
    for( ImGuiWindow* window : ImGui::GetCurrentContext()->Windows )
    {
        if( window->DockNode && !( window->Flags & ImGuiWindowFlags_ChildWindow ) && !kLayoutPanels.count( window->Name ) && std::strcmp( window->Name, "Asset Directory" ) != 0 )
        {
            m_pendingDockDocuments.push_back( window->Name );
        }
    }
    ImGui::ClearIniSettings();
    // Unattended runs start every workspace from its template, whatever a user saved (ME-307).
    const Path layout = AutomationRunner::IsUnattendedRun() ? Path() : GetWorkspaceLayoutPath( InWorkspace.Id );
    m_pendingDockTemplate.clear();
    if( layout.Exists || InWorkspace.Template == "Scene" || !kTemplatePanels.count( InWorkspace.Template ) )
    {
        if( layout.Exists ) LoadLayoutFile( layout.FullPath.c_str() );
        else if( DefaultLayoutFilePath.Exists ) LoadLayoutFile( DefaultLayoutFilePath.FullPath.c_str() );
        // A full dock rebuild next frame: with panels open and docked, patching the live tree trips
        // ImGui's tab bar asserts.
        RequestDockRebuild();
        m_pendingDockDocuments.clear();
        return layout.Exists;
    }
    m_pendingDockTemplate = InWorkspace.Template;
    return false;
}


void Havana::BuildTemplateDock( const std::string& InTemplate, unsigned int InDockSpaceId )
{
    // Runs before this frame's DockSpace, so no docked window has drawn into the old tree yet.
    ImGui::DockBuilderRemoveNode( InDockSpaceId );
    ImGui::DockBuilderAddNode( InDockSpaceId, ImGuiDockNodeFlags_DockSpace );
    ImGui::DockBuilderSetNodeSize( InDockSpaceId, DockSize );

    ImGuiID center = InDockSpaceId;
    const auto split = [&center]( ImGuiDir InDir, float InRatio ) { return ImGui::DockBuilderSplitNode( center, InDir, InRatio, nullptr, &center ); };
    const auto dock = []( const char* InPanel, ImGuiID InNode ) { ImGui::DockBuilderDockWindow( PanelWindowTitle( InPanel ).c_str(), InNode ); };
    std::vector<std::pair<ImGuiID, std::string>> selected;     // node, the tab shown first (window titles)
    if( InTemplate == "Animation" )
    {
        const ImGuiID bottom = split( ImGuiDir_Down, 0.3f );
        const ImGuiID left = split( ImGuiDir_Left, 0.18f );
        ImGuiID right = split( ImGuiDir_Right, 0.24f );
        const ImGuiID rightBottom = ImGui::DockBuilderSplitNode( right, ImGuiDir_Down, 0.45f, nullptr, &right );
        dock( "Sequencer", bottom );
        dock( "Hierarchy", left );
        dock( "Properties", right );
        dock( "Preview", rightBottom );
        dock( "World View", center );
        dock( "Game View", center );
        selected.emplace_back( center, "World View" );
    }
    else if( InTemplate == "Shader" )
    {
        const ImGuiID bottom = split( ImGuiDir_Down, 0.28f );
        ImGuiID right = split( ImGuiDir_Right, 0.26f );
        const ImGuiID rightBottom = ImGui::DockBuilderSplitNode( right, ImGuiDir_Down, 0.5f, nullptr, &right );
        dock( "Asset Browser", bottom );
        dock( "Log", bottom );
        dock( "Preview", right );
        dock( "Properties", rightBottom );
        // Shader graphs open as tabs beside the World View.
        dock( "World View", center );
        selected.emplace_back( bottom, "Asset Directory" );
    }
    else if( InTemplate == "UI" )
    {
        const ImGuiID bottom = split( ImGuiDir_Down, 0.28f );
        const ImGuiID right = split( ImGuiDir_Right, 0.34f );
        dock( "Localization", bottom );
        dock( "Asset Browser", bottom );
        dock( "Log", bottom );
        dock( "UI Designer", right );
        // The designer previews in the Game View.
        dock( "Game View", center );
        selected.emplace_back( bottom, "Localization" );
    }
    else if( InTemplate == "Debug" )
    {
        const ImGuiID bottom = split( ImGuiDir_Down, 0.36f );
        ImGuiID left = split( ImGuiDir_Left, 0.18f );
        const ImGuiID leftBottom = ImGui::DockBuilderSplitNode( left, ImGuiDir_Down, 0.5f, nullptr, &left );
        ImGuiID right = split( ImGuiDir_Right, 0.26f );
        const ImGuiID rightBottom = ImGui::DockBuilderSplitNode( right, ImGuiDir_Down, 0.4f, nullptr, &right );
        dock( "Profiler", bottom );
        dock( "Resource Monitor", bottom );
        dock( "Hierarchy", left );
        dock( "Properties", leftBottom );
        dock( "Log", right );
        dock( "Error List", rightBottom );
        dock( "Game View", center );
        dock( "World View", center );
        selected.emplace_back( bottom, "Profiler" );
        selected.emplace_back( center, "Game View" );
    }
    else if( InTemplate == "Network" )
    {
        const ImGuiID bottom = split( ImGuiDir_Down, 0.42f );
        const ImGuiID right = split( ImGuiDir_Right, 0.28f );
        dock( "Net Debugger", bottom );
        dock( "Profiler", bottom );
        dock( "Log", right );
        dock( "Game View", center );
        dock( "World View", center );
        selected.emplace_back( bottom, "Net Debugger" );
        selected.emplace_back( center, "Game View" );
    }
    for( const std::string& document : m_pendingDockDocuments )
    {
        ImGui::DockBuilderDockWindow( document.c_str(), center );
    }
    m_pendingDockDocuments.clear();
    // Closed panels still pointing into the old tree reopen as tabs in the middle, not floating.
    for( ImGuiWindow* window : ImGui::GetCurrentContext()->Windows )
    {
        if( window->DockId != 0 && !window->DockNode && !ImGui::DockBuilderGetNode( window->DockId )
            && !( window->Flags & ( ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip ) ) )
        {
            ImGui::DockBuilderDockWindow( window->Name, center );
        }
    }
    ImGui::DockBuilderFinish( InDockSpaceId );
    m_pendingTabSelections.clear();
    for( const auto& [node, tab] : selected )
    {
        if( ImGuiDockNode* dockNode = ImGui::DockBuilderGetNode( node ) ) dockNode->SelectedTabId = ImHashStr( tab.c_str() );
        m_pendingTabSelections.push_back( tab );
    }
    CLog::Log( CLog::LogType::Info, "Layout: built the " + InTemplate + " layout" );
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
    // Two frames: first the panels go back to the workspace template's visibility (closing a docked
    // window and replacing the dock tree in one frame trips ImGui's tab bar asserts), then the
    // template's layout loads.
    const Workspace* workspace = FindWorkspace( ActiveWorkspace );
    if( !workspace )
    {
        m_resetLayoutPending = false;
        m_resetLayoutPhase = 0;
        return;
    }
    if( m_resetLayoutPhase == 0 )
    {
        ApplyTemplatePanels( workspace->Template );
        m_resetLayoutPhase = 1;
        return;
    }
    m_resetLayoutPhase = 0;
    m_resetLayoutPending = false;
    std::error_code error;
    std::filesystem::remove( GetWorkspaceLayoutPath( ActiveWorkspace ).FullPath, error );
    json& workspaces = EditorConfig::GetInstance().Preferences["Workspaces"];
    if( workspaces.is_object() )
    {
        workspaces.erase( std::to_string( ActiveWorkspace ) );
    }
    LoadWorkspaceDock( *workspace );
    RestoreWorkspacePanelVisibility( ActiveWorkspace );
    if( !AutomationRunner::IsUnattendedRun() )
    {
        ImGui::SaveIniSettingsToDisk( EngineConfigFilePath.FullPath.c_str() );
        EditorConfig::GetInstance().Save();
    }
    CLog::Log( CLog::LogType::Info, "Layout: reset workspace " + std::to_string( ActiveWorkspace ) + " (" + workspace->Name + ", " + workspace->Template + " layout)" );
}


void Havana::RestoreWorkspacePanelVisibility( int InWorkspace )
{
    const json& preferences = EditorConfig::GetInstance().Preferences;
    const auto workspace = preferences.find( "Workspaces" );
    const bool stored = !AutomationRunner::IsUnattendedRun() && workspace != preferences.end() && workspace->is_object() && workspace->contains( std::to_string( InWorkspace ) );
    if( !stored )
    {
        // A workspace nobody has arranged yet opens its template's panels.
        const Workspace* found = FindWorkspace( InWorkspace );
        ApplyTemplatePanels( found ? found->Template : std::string( "Scene" ) );
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


void Havana::SwitchWorkspace( int InId )
{
    const Workspace* target = FindWorkspace( InId );
    if( !target || InId == ActiveWorkspace ) return;

    SaveWorkspaceLayout( ActiveWorkspace );
    // The panels change now, the layout before the next frame (two steps, as Reset Layout: closing a
    // docked window and replacing the dock tree in one frame trips ImGui's tab bar asserts).
    m_pendingWorkspaceDock = InId;
    RestoreWorkspacePanelVisibility( InId );
    ActiveWorkspace = InId;
    EditorConfig::GetInstance().SetPreference( "ActiveWorkspace", ActiveWorkspace );
    RenamingWorkspace = 0;
    CLog::Log( CLog::LogType::Info, "Layout: workspace " + target->Name );

    if( !AutomationRunner::IsUnattendedRun() )
    {
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
        Camera* mainCamera = GetEngine().GetMainCamera();
        Moonlight::CameraData* camData = mainCamera ? renderer.GetCameraCache().Get( mainCamera->GetCameraId() ) : nullptr;
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
        if( Camera* mainCamera = GetEngine().GetMainCamera() ) mainCamera->OutputSize = gameOutputSize;
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
