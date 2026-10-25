#include "Havana.h"
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
#include "Widgets/SequenceWidget.h"
#include "Widgets/ProfilerWidget.h"
#include "Widgets/SettingsWidgets.h"
#include "Widgets/BuildGameWidget.h"
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
    actions.Register( { "View.Profiler", "Profiler", "View", 0, 0, [this]() { ShowWidget( "Profiler" ); } } );
    ReflectionUI::RegisterDefaultDrawers();
    actions.Register( { "View.AssetSearch", "Quick Asset Search", "View", ImGuiMod_Shortcut | ImGuiKey_Space, 0, [this]() { AssetBrowser->RequestOverlay( nullptr ); } } );

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
    // The dock layout is per user (.tmp/imgui.ini, never committed). The project's
    // Assets/Config/imgui.cfg is the default layout a fresh checkout starts from; it's never written.
    std::error_code ec;
    std::filesystem::create_directories( ".tmp", ec );
    EngineConfigFilePath = Path( ".tmp/imgui.ini", true );
    const Path defaultLayout( "Assets/Config/imgui.cfg" );
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    if( EngineConfigFilePath.Exists )
    {
        ImGui::LoadIniSettingsFromDisk( EngineConfigFilePath.FullPath.c_str() );
    }
    else if( defaultLayout.Exists )
    {
        ImGui::LoadIniSettingsFromDisk( defaultLayout.FullPath.c_str() );
    }
    // Unattended runs use the layout without ever writing it back.
    io.IniFilename = AutomationRunner::IsUnattendedRun() ? nullptr : EngineConfigFilePath.FullPath.c_str();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
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
    Sequencer->Render();
    Profiler->Render();
    Preferences->Render();
    ProjectSettingsView->Render();
    BuildGameView->Render();
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
        int cameraId = Camera::CurrentCamera->GetCameraId();
        BGFXRenderer& renderer = GetEngine().GetRenderer();
        Moonlight::CameraData* camData = renderer.GetCameraCache().Get( cameraId );
        GameSceneView->SetData( *camData );
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
                gameInput.Resume();
                GetInput().Stop();
            }
        }

        // #TODO: Remove this
        // Update: This crashes macOS, but is needed for to display the game as the correct size in the editor
        Vector2 gameOutputSize = GetGameOutputSize();
        Camera::CurrentCamera->OutputSize = gameOutputSize;
        if( camData && camData->Buffer && ((int)gameOutputSize.x != camData->Buffer->Width || (int)gameOutputSize.y != camData->Buffer->Height ) )
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
