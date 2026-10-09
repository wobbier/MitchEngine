#include "PCH.h"
#include "Engine.h"
#include "CLog.h"
#include "Config/EngineConfig.h"
#include "Window/UWPWindow.h"
#include "Events/EventManager.h"
#include "Cores/PhysicsCore.h"
#include "Cores/Cameras/CameraCore.h"
#include "Cores/SceneCore.h"
#include "Cores/Rendering/RenderCore.h"
#include "Game.h"
#include "Window/IWindow.h"
#include "Input.h"
#include "Components/Transform.h"
#include "Dementia.h"
#include "Events/SceneEvents.h"
#include "Components/Camera.h"
#include "Components/Cameras/FlyingCamera.h"
#include "Cores/Cameras/FlyingCameraCore.h"
#include "Cores/AudioCore.h"
#include "Cores/UI/UICore.h"
#include "Cores/Scripting/ScriptCore.h"

#if USING( ME_EDITOR_WIN64 )
#include "Utils/StringUtils.h"
#include <fileapi.h>
#endif
#include "Resource/ResourceCache.h"
#include "optick.h"
#include "Work/Burst.h"
#include "Profiling/BasicFrameProfile.h"
#include "Renderer.h"
#include "Window/SDLWindow.h"
#include "Path.h"
#include "SDL.h"
#include "SDL_video.h"
#include <imgui.h>
#include <Debug/DebugDrawer.h>
#include <Debug/DebugDraw.h>
#include "Events/PlatformEvents.h"
#include "Scripting/ScriptEngine.h"
#include "Core/Assert.h"
#include "Events/EditorEvents.h"
#include "Core/CommandLine.h"
#include "Core/CrashHandler.h"
#include "Resource/FileWatcher.h"
#include "Resource/AssetDatabase.h"
#include "World/SceneSerializer.h"
#include <chrono>
#include <thread>
#include <algorithm>
#include <cmath>

Engine& GetEngine()
{
    return Engine::GetInstance();
}

Engine::Engine()
    : Running( true )
{
    std::vector<TypeId> events;
    events.push_back( LoadSceneEvent::GetEventId() );
    events.push_back( WindowMovedEvent::GetEventId() );
    EventManager::GetInstance().RegisterReceiver( this, events );
}

Engine::~Engine()
{
}

extern bool ImGui_ImplSDL2_InitForD3D( SDL_Window* window );
extern bool ImGui_ImplSDL2_InitForMetal( SDL_Window* window );
extern bool ImGui_ImplSDL2_InitForOpenGL(SDL_Window* window, void* sdl_gl_context);
extern bool ImGui_ImplSDL2_InitForVulkan( SDL_Window* window );
extern bool ImGui_ImplWin32_Init( void* window );
void Engine::Init( Game* game )
{
    OPTICK_EVENT( "Engine::Init" );
    if( m_isInitialized || !game )
    {
        return;
    }

    m_game = game;

    CLog::GetInstance().SetLogFile( "Engine.txt" );
#if USING( ME_RETAIL )
    CLog::GetInstance().SetLogVerbosity( CLog::LogType::Info );
#else
    CLog::GetInstance().SetLogVerbosity( CLog::LogType::Trace );
#endif
    CrashHandler::Install( ".tmp/Crashes" );
    CLog::GetInstance().Log( CLog::LogType::Info, "Starting the MitchEngine." );
    Path engineCfg( "Assets\\Config\\Engine.cfg" );

#if USING( ME_EDITOR )
    if( engineCfg.FullPath.rfind( "Engine" ) != -1 )
    {
        Path gameEngineCfgPath( "Assets\\Config\\Engine.cfg", true );
        if( !gameEngineCfgPath.Exists )
        {
#if USING( ME_PLATFORM_WIN64 )
            CreateDirectory( StringUtils::ToWString( std::string( gameEngineCfgPath.GetDirectory() ) ).c_str(), NULL );
#endif
            File gameEngineCfg = File( engineCfg );
            File newGameConfig( gameEngineCfgPath );
            newGameConfig.Write( gameEngineCfg.Read() );
        }
    }
#endif


    std::function<void( const Vector2& )> ResizeFunc = [this]( const Vector2& NewSize )
    {
        if( NewRenderer )
        {
            NewRenderer->WindowResized( NewSize );
        }
        if( UI )
        {
            if( Camera::CurrentCamera )
            {
                UI->OnResize( Camera::CurrentCamera->OutputSize );
            }
        }
        engineConfig.WindowSize = NewSize;
        WindowResizedEvent evt;
        evt.NewSize = NewSize;
        evt.Fire();
    };

    {
        OPTICK_EVENT( "Engine::Init::LoadConfig" );
        engineConfig = EngineConfig( engineCfg );
        engineConfig.OnLoadConfig( engineConfig.Root );

        m_automation.Init();

        // Timing settings: Engine.cfg keys, overridable from the command line.
        const json& configRoot = engineConfig.Root;
        if( configRoot.contains( "FixedTimeStep" ) && configRoot["FixedTimeStep"].is_number() )
        {
            SetFixedTimeStep( configRoot["FixedTimeStep"].get<float>() );
        }
        if( configRoot.contains( "MaxFrameRate" ) && configRoot["MaxFrameRate"].is_number() )
        {
            SetMaxFrameRate( configRoot["MaxFrameRate"].get<float>() );
        }
        if( CommandLine::Has( "--fixed-step" ) )
        {
            SetFixedTimeStep( CommandLine::GetFloat( "--fixed-step", m_fixedTimeStep ) );
        }
        if( CommandLine::Has( "--max-fps" ) )
        {
            SetMaxFrameRate( CommandLine::GetFloat( "--max-fps", 0.f ) );
        }
        if( CommandLine::Has( "--width" ) && CommandLine::Has( "--height" ) )
        {
            engineConfig.WindowSize = Vector2( static_cast<float>( CommandLine::GetInt( "--width" ) ), static_cast<float>( CommandLine::GetInt( "--height" ) ) );
        }
    }
#if USING( ME_PLATFORM_WIN64 ) || USING( ME_PLATFORM_MACOS ) || USING( ME_PLATFORM_LINUX )
    {
        OPTICK_EVENT( "Engine::Init::CreateWindow" );
        GameWindow = new SDLWindow( engineConfig.GetValue( "Title" ), ResizeFunc, engineConfig.WindowPosition.x, engineConfig.WindowPosition.y, engineConfig.WindowSize );
    }
#endif

#if USING( ME_PLATFORM_UWP )
    int WindowWidth = 1280;
    int WindowHeight = 720;
    GameWindow = new SDLWindow( "MitchEngine", ResizeFunc, 500, 300, Vector2( WindowWidth, WindowHeight ) );
    //GameWindow = new UWPWindow("MitchEngine", 1920, 1080, ResizeFunc);
#endif
#if USING( ME_EDITOR_WIN64 )
    GameWindow->SetBorderless( true );
#endif

    CLog::GetInstance().Log( CLog::LogType::Info, "Starting the Renderer." );
    {
        OPTICK_EVENT( "Engine::Init::RendererCreate" );
        NewRenderer = new BGFXRenderer();
        RendererCreationSettings settings;
        settings.WindowPtr = GameWindow->GetWindowPtr();
#if USING( ME_PLATFORM_LINUX )
        settings.DisplayPtr = static_cast<SDLWindow*>( GameWindow )->GetDisplayPtr();
        settings.WindowType = static_cast<SDLWindow*>( GameWindow )->GetWindowType();
#endif
        settings.InitialSize = engineConfig.WindowSize;
        NewRenderer->Create( settings );
        NewRenderer->EnableUIComposite = !CommandLine::Has( "--no-ui" );
    }
    CLog::GetInstance().Log( CLog::LogType::Info, "After the Renderer." );
#if USING( ME_IMGUI )
#if USING( ME_PLATFORM_WIN64 )
    ImGui_ImplSDL2_InitForD3D( static_cast<SDLWindow*>( GameWindow )->WindowHandle );
#elif USING( ME_PLATFORM_MACOS )
    ImGui_ImplSDL2_InitForMetal( static_cast<SDLWindow*>( GameWindow )->WindowHandle );
#elif USING( ME_PLATFORM_LINUX )
    ImGui_ImplSDL2_InitForVulkan( static_cast<SDLWindow*>( GameWindow )->WindowHandle );
#endif
#endif
    //m_renderer = new Moonlight::Renderer();
    //m_renderer->WindowResized(GameWindow->GetSize());

    {
        OPTICK_EVENT( "Engine::Init::CreateCores" );
        GameWorld = MakeShared<World>();
        Cameras = new CameraCore();
        SceneNodes = new SceneCore();
        ModelRenderer = new RenderCore();
        AudioThread = new AudioCore();
        UI = new UICore( GameWindow, NewRenderer );
    }

    NewRenderer->SetGuizmoDrawCallback( [this]( DebugDrawer* drawer )
        {
            std::vector<BaseCore*> cores = GetWorld().lock()->GetAllCores();
            for( BaseCore* core : cores )
            {
                core->OnDrawGuizmo( drawer );
            }
        } );

#if USING( ME_GAME_TOOLS )
    m_debugTools.Init();
#endif

    InitGame();

    ResizeFunc( engineConfig.WindowSize );

    // Register systems
    {
        systemRegistry.RegisterSystem( this );
        systemRegistry.RegisterSystem( NewRenderer );
        systemRegistry.RegisterSystem( &Jobs::JobSystem::Get() );
    }
    updateContext.m_SystemRegistry = &systemRegistry;

    m_isInitialized = true;
}

void Engine::InitGame()
{
    OPTICK_EVENT( "Engine::InitGame" );
    {
        OPTICK_EVENT( "Engine::InitGame::AddCores" );
        GameWorld->AddCore<CameraCore>( *Cameras );
        GameWorld->AddCore<SceneCore>( *SceneNodes );
        GameWorld->AddCore<RenderCore>( *ModelRenderer );
        GameWorld->AddCore<AudioCore>( *AudioThread );
        GameWorld->AddCore<UICore>( *UI );
    }

    YIKES("Engine::InitGame");
    {
        OPTICK_EVENT( "Engine::InitGame::OnInitialize" );
        m_game->OnInitialize();
    }
}

void Engine::StopGame()
{
    m_game->OnEnd();
}

#if USING( ME_IMGUI )
extern void ImGui_ImplSDL2_NewFrame();
#endif

void Engine::Run()
{
#if USING( ME_TOOLS )
    // Hot reload: watch the asset trees and reimport/reload changed assets at frame start.
    AssetDatabase::Get().Refresh( { "Assets", "Engine/Assets" } );
    m_assetWatcher.Start( { "Assets", "Engine/Assets" } );
#endif

    m_game->OnStart();

    if( CommandLine::Has( "--scene" ) )
    {
        LoadScene( CommandLine::GetString( "--scene" ) );
    }

    GameClock.Reset();
    m_fixedAccumulator = 0.0;

    // Game loop
    forever
    {
        OPTICK_FRAME( "MainLoop" );
        const auto frameStartTime = std::chrono::steady_clock::now();
#if USING( ME_BASIC_PROFILER )
        FrameProfile::GetInstance().Start();
#endif
        // Check and call events
        GameWindow->ParseMessageQueue();

        if( GameWindow->ShouldClose() )
        {
            if( m_forceQuit || m_game->OnQuitRequested() )
            {
                StopGame();
                break;
            }
            GameWindow->CancelClose();
        }

        {
            OPTICK_EVENT( "EventManager", Optick::Category::Cloth );
            EventManager::GetInstance().FirePendingEvents();
        }

#if USING( ME_TOOLS )
        PollAssetChanges();
#endif

        // Frame timing: clamp hitches (debugger breaks, loading) so the simulation never tries to
        // catch up on seconds of backlog, then apply pause / time scale.
        float frameSeconds = 0.f;
        float scaledSeconds = 0.f;
        {
            OPTICK_EVENT( "Clock" );
            GameClock.Update();
            frameSeconds = static_cast<float>( std::min( GameClock.GetDeltaSecondsPrecise(), static_cast<double>( m_maxFrameDelta ) ) );
            if( m_isPaused )
            {
                // Step Frame advances exactly one fixed step while paused.
                scaledSeconds = m_stepRequested ? m_fixedTimeStep : 0.f;
                m_stepRequested = false;
            }
            else
            {
                scaledSeconds = frameSeconds * m_timeScale;
            }
            DeltaTime = scaledSeconds;
            updateContext.FixedDeltaTime = m_fixedTimeStep;
            updateContext.BeginFrame( scaledSeconds, frameSeconds, m_timeScale, m_isPaused );
        }

        GetInput().Update();
#if USING( ME_EDITOR )
        GetEditorInput().Update();
#endif

        {
            const float deltaTime = scaledSeconds;

#if USING( ME_IMGUI )
            {
#if USING( ME_EDITOR )
                Input& input = GetEditorInput();
#else
                Input& input = GetInput();
#endif

                ImGuiIO& io = ImGui::GetIO();
                Vector2 mousePos = input.GetMousePosition();
                if( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
                {
                    OPTICK_EVENT( "GetGlobalMousePosition" );
                    // Multi-viewport mode: mouse position in OS absolute coordinates (io.MousePos is (0,0) when the mouse is on the upper-left of the primary monitor)
                    mousePos = input.GetGlobalMousePosition();
                }
                m_frameRenderSettings.MousePosition = mousePos;
                m_frameRenderSettings.WasLeftPressed = input.WasMouseButtonPressed( MouseButton::Left );
                ImGui_ImplSDL2_NewFrame();

                NewRenderer->BeginFrame( mousePos, ( input.IsMouseButtonDown( MouseButton::Left ) ? 0x01 : 0 )
                    | ( input.IsMouseButtonDown( MouseButton::Right ) ? 0x02 : 0 )
                    | ( input.IsMouseButtonDown( MouseButton::Middle ) ? 0x04 : 0 )
                    , (int32_t)input.GetMouseScrollOffset().y
                    , GameWindow->GetSize()
                    , -1
                    , Moonlight::RenderView::ImGuiMain );
            }
#endif

            GameWorld->Simulate();

            if( m_frameRenderSettings.PickCompleted )
            {
                PickingEvent evt;
                evt.RawEntityID = m_frameRenderSettings.RequestedEntityID;
                evt.Fire();
                m_frameRenderSettings.RequestedEntityID = 0;
                m_frameRenderSettings.PickCompleted = false;
            }
            

#if USING( ME_GAME_TOOLS )
        m_debugTools.Render();
#endif

            // Fixed-step simulation (physics, deterministic gameplay): zero or more steps per frame.
            {
                OPTICK_EVENT( "FixedUpdate" );
                ME_FRAMEPROFILE_SCOPED( "Physics", ProfileCategory::Physics );
                m_fixedAccumulator += scaledSeconds;
                int steps = 0;
                while( m_fixedAccumulator >= m_fixedTimeStep && steps < m_maxFixedStepsPerFrame )
                {
                    updateContext.IsFixedStepActive = true;
                    GameWorld->FixedUpdateLoadedCores( updateContext );
                    m_game->OnFixedUpdate( updateContext );
                    updateContext.IsFixedStepActive = false;
                    GameWorld->Simulate();
                    m_fixedAccumulator -= m_fixedTimeStep;
                    ++steps;
                }
                if( steps == m_maxFixedStepsPerFrame && m_fixedAccumulator >= m_fixedTimeStep )
                {
                    // Too far behind: drop the backlog rather than spiral.
                    m_fixedAccumulator = std::fmod( m_fixedAccumulator, static_cast<double>( m_fixedTimeStep ) );
                }
                updateContext.InterpolationAlpha = static_cast<float>( m_fixedAccumulator / m_fixedTimeStep );
            }

            // Update Loaded Cores
            {
                ME_FRAMEPROFILE_SCOPED( "Game Cores", ProfileCategory::Game );
                GameWorld->UpdateLoadedCores( updateContext );
                GameWorld->Simulate();
            }

            // Update Cameras
            {
                OPTICK_EVENT( "SceneNodes->Update" );
                ME_FRAMEPROFILE_SCOPED( "SceneNodes", ProfileCategory::UI );
                SceneNodes->Update( updateContext );
            }

            // Update Game Application
            {
                ME_FRAMEPROFILE_SCOPED( "Game", ProfileCategory::Game );
                OPTICK_CATEGORY( "MainLoop::GameUpdate", Optick::Category::GameLogic );
                m_game->OnUpdate( updateContext );
                GameWorld->Simulate();
            }

            // Update Audio
            {

                AudioThread->Update( deltaTime );
            }

            // Model Renderer Update
            {
                ME_FRAMEPROFILE_SCOPED( "ModelRenderer", ProfileCategory::Rendering );
                ModelRenderer->Update( updateContext );
            }

            // UI Update
            {
                OPTICK_CATEGORY( "UICore::Update", Optick::Category::Rendering )
                ME_FRAMEPROFILE_SCOPED( "UI", ProfileCategory::UI );
                // editor only?
                if( UI )
                {
                    if( Camera::CurrentCamera )
                    {
                        UI->OnResize( Camera::CurrentCamera->OutputSize );
                    }
                    UI->Update( updateContext );
                }
            }

            // Late Update
            {
                OPTICK_EVENT( "LateUpdate" );
                GameWorld->LateUpdateLoadedCores( updateContext );
                GameWorld->Simulate();
                Cameras->Update( updateContext );
                SceneNodes->LateUpdate( updateContext );
                Cameras->LateUpdate( updateContext );
                AudioThread->LateUpdate( updateContext );
                ModelRenderer->LateUpdate( updateContext );
                UI->LateUpdate( updateContext );
            }

            // Render
            {
                OPTICK_EVENT( "Render" );
                m_game->PreRender();
#if !USING( ME_EDITOR )
                EditorCamera.OutputSize = GetWindow()->GetSize();
#if USING( ME_BASIC_PROFILER )
                FrameProfile::GetInstance().Render( { GameWindow->GetClientPosition().x + 10.f, ( GameWindow->GetClientPosition().y + GameWindow->GetClientSize().y - FrameProfile::kMinProfilerSize - 10.f ) }, { GameWindow->GetClientSize().x - 20, (float)FrameProfile::kMinProfilerSize } );
#endif
#endif
                ME_FRAMEPROFILE_START( "UI Render", ProfileCategory::UI );
                UI->Render();
                ME_FRAMEPROFILE_STOP( "UI Render" );
                ME_FRAMEPROFILE_START( "Render", ProfileCategory::Rendering );
                NewRenderer->Render( EditorCamera, m_frameRenderSettings );
                ME_FRAMEPROFILE_STOP( "Render" );
                DebugDraw::EndFrame( updateContext.GetUnscaledDeltaTime() );
                UI->PostRender( updateContext );
                m_game->PostRender();
            }

            if( m_automation.IsActive() )
            {
                const double frameMs = std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - frameStartTime ).count();
                if( m_automation.OnFrameEnd( *NewRenderer, frameMs ) )
                {
                    StopGame();
                    break;
                }
            }

#if USING( ME_BASIC_PROFILER )
            // This makes the profiler overview data to be delayed for a frame, but takes the renderer into account.
            {
                static float fpsTime = 0;
                fpsTime += frameSeconds;
                if( fpsTime > 1.f )
                {
                    FrameProfile::GetInstance().Dump();
                    fpsTime -= 1.f;
                }
            }
#endif

#if USING( ME_BASIC_PROFILER )
            FrameProfile::GetInstance().End( frameSeconds );
#endif
            GetInput().PostUpdate();
#if USING ( ME_EDITOR )
            GetEditorInput().PostUpdate();
#endif
        }
        ResourceCache::GetInstance().Dump();

        LimitFrameRate( frameStartTime );
    }

    if( m_automation.IsActive() )
    {
        // Unattended runs must not clobber the user's window config.
        m_automation.Shutdown();
    }
    else
    {
        engineConfig.Save();
    }
    Shutdown();
}


#if USING( ME_TOOLS )
void Engine::PollAssetChanges()
{
    std::vector<FileWatcher::Change> changes = m_assetWatcher.ConsumeChanges();
    if( changes.empty() )
    {
        return;
    }
    OPTICK_EVENT( "Engine::PollAssetChanges" );
    std::vector<std::string> paths;
    for( const FileWatcher::Change& change : changes )
    {
        if( change.Type != FileWatcher::ChangeType::Removed )
        {
            paths.push_back( change.FullPath );
        }
        if( change.FullPath.size() > 7 && change.FullPath.compare( change.FullPath.size() - 7, 7, ".prefab" ) == 0 )
        {
            SceneSerializer::ClearPrefabCache();
        }
    }
    for( const std::string& reloaded : ResourceCache::GetInstance().OnFilesChanged( paths ) )
    {
        CLog::Log( CLog::LogType::Info, "Hot reloaded: " + reloaded );
    }
}
#endif


void Engine::Shutdown()
{
    OPTICK_EVENT( "Engine::Shutdown" );
#if USING( ME_TOOLS )
    m_assetWatcher.Stop();
#endif
    CLog::Log( CLog::LogType::Info, "Shutting down." );

    // Components get OnDisable/OnDestroy while every engine system is still alive.
    if( GameWorld )
    {
        GameWorld->Stop();
        GameWorld->Destroy();
    }

    Jobs::JobSystem::Get().Shutdown();
    CLog::GetInstance().Flush();
}

void Engine::LimitFrameRate( std::chrono::steady_clock::time_point frameStart )
{
    if( m_maxFrameRate <= 0.f )
    {
        return;
    }
    OPTICK_EVENT( "FrameRateLimit" );
    const auto frameEnd = frameStart + std::chrono::duration_cast<std::chrono::steady_clock::duration>( std::chrono::duration<double>( 1.0 / m_maxFrameRate ) );
    // Sleep most of the remainder, then spin for precision.
    const auto sleepUntil = frameEnd - std::chrono::milliseconds( 1 );
    if( std::chrono::steady_clock::now() < sleepUntil )
    {
        std::this_thread::sleep_until( sleepUntil );
    }
    while( std::chrono::steady_clock::now() < frameEnd )
    {
        std::this_thread::yield();
    }
}


void Engine::SetTimeScale( float InTimeScale )
{
    m_timeScale = std::max( 0.f, InTimeScale );
}


void Engine::SetPaused( bool InPaused )
{
    m_isPaused = InPaused;
    m_stepRequested = false;
}


void Engine::StepFrame()
{
    m_stepRequested = true;
}


void Engine::SetFixedTimeStep( float InSeconds )
{
    m_fixedTimeStep = std::clamp( InSeconds, 1.f / 1000.f, 1.f / 5.f );
}


void Engine::SetMaxFrameRate( float InFramesPerSecond )
{
    m_maxFrameRate = std::max( 0.f, InFramesPerSecond );
}


bool Engine::OnEvent( const BaseEvent& evt )
{
    if( evt.GetEventId() == LoadSceneEvent::GetEventId() )
    {
        const LoadSceneEvent& loadSceneEvent = static_cast<const LoadSceneEvent&>( evt );
        //InputEnabled = test.Enabled;
        LoadScene( loadSceneEvent.Level );
        if( loadSceneEvent.Callback )
        {
            loadSceneEvent.Callback();
        }
    }

    if( evt.GetEventId() == WindowMovedEvent::GetEventId() )
    {
        const WindowMovedEvent& test = static_cast<const WindowMovedEvent&>( evt );
        engineConfig.WindowPosition = test.NewPosition;
    }

    return false;
}

BGFXRenderer& Engine::GetRenderer() const
{
    return *NewRenderer;
}

std::weak_ptr<World> Engine::GetWorld() const
{
    return GameWorld;
}

const bool Engine::IsInitialized() const
{
    return m_isInitialized;
}

bool Engine::IsRunning() const
{
    return true;
}

void Engine::Quit( bool InForce )
{
    m_forceQuit = m_forceQuit || InForce;
    GameWindow->Exit();
}

IWindow* Engine::GetWindow()
{
    return GameWindow;
}

Game* Engine::GetGame() const
{
    return m_game;
}

EngineConfig& Engine::GetConfig()
{
    return engineConfig;
}

Input& Engine::GetInput()
{
    return m_input;
}

Jobs::JobSystem& Engine::GetJobSystem()
{
    return Jobs::JobSystem::Get();
}


void Engine::LoadScene( const std::string& SceneFile )
{
    LoadSceneInternal( SceneFile, nullptr );
}


void Engine::LoadSceneFromData( const json& InData, const std::string& InFilePath )
{
    LoadSceneInternal( InFilePath, &InData );
}


void Engine::LoadSceneInternal( const std::string& SceneFile, const json* InData )
{
    OPTICK_EVENT( "Engine::LoadScene" );
    Cameras->Init();
    if( CurrentScene )
    {
        OPTICK_EVENT( "Engine::LoadScene::UnloadPrevious" );
        CurrentScene->UnLoad();
        delete CurrentScene;
        CurrentScene = nullptr;
    }

    {
        OPTICK_EVENT( "Engine::LoadScene::WorldUnload" );
        GameWorld->Unload();
    }
    SceneNodes->Init();
    CurrentScene = new Scene( SceneFile );
    if( InData )
    {
        CurrentScene->PreloadedData = *InData;
    }

    {
        OPTICK_EVENT( "Engine::LoadScene::ParseJSON" );
        if( !CurrentScene->Load( GameWorld ) && !CurrentScene->IsNewScene() && !InData )
        {
            ME_ASSERT_MSG( false, "Failed to load scene." );
        }
    }

#if USING( ME_SCRIPTING )
    ScriptEngine::SetWorld( GetWorld() );
#endif

    GameWorld->AddCore<UICore>(*UI);

    GameWorld->Simulate();

    SceneLoadedEvent evt;
    evt.LoadedScene = CurrentScene;
    evt.Fire();

#if !USING( ME_EDITOR )
    GameWorld->Simulate();
    GameWorld->Start();
#endif
}

#if USING( ME_EDITOR )

Input& Engine::GetEditorInput()
{
    return m_editorInput;
}

#endif
