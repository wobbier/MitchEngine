#include "PCH.h"
#include "Engine.h"
#include "CLog.h"
#include "Config/EngineConfig.h"
#include "Events/EventManager.h"
#include "Cores/PhysicsCore.h"
#include "Cores/Physics2DCore.h"
#include "Cores/AnimationCore.h"
#include "Cores/NavigationCore.h"
#include "Cores/Cameras/CameraCore.h"
#include "Cores/SceneCore.h"
#include "Cores/Rendering/RenderCore.h"
#include "Cores/Rendering/ParticleCore.h"
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
#include "Components/Graphics/Model.h"
#include "Audio/AudioOcclusion.h"
#include "Input/Gamepads.h"
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
#include "Engine/ProjectSettings.h"
#include "Profiling/FrameStats.h"
#include "Events/PlatformEvents.h"
#include "Scripting/ScriptEngine.h"
#include "Core/Assert.h"
#include "Events/EditorEvents.h"
#include "Core/CommandLine.h"
#include "Core/CrashHandler.h"
#include "Resource/FileWatcher.h"
#include "Graphics/ShaderDependencies.h"
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
        ProjectSettings::Get().Load();

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
        if( CommandLine::Has( "--frame-time" ) )
        {
            // Simulated time per frame, whatever the real frame took: repeatable automated captures.
            m_fixedFrameDelta = std::max( 0.f, CommandLine::GetFloat( "--frame-time", 0.f ) );
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
        Particles = new ParticleCore();
        Physics = new PhysicsCore();
        Physics2D = new Physics2DCore();
        Animation = new AnimationCore();
        Navigation = new NavigationCore();
        // Automated runs (captures, editor scripts) and --no-audio never play through the speakers.
        const bool silentAudio = CommandLine::Has( "--no-audio" ) || ( AutomationRunner::IsUnattendedRun() && !CommandLine::Has( "--audio" ) );
        AudioThread = new AudioCore( silentAudio ? AudioOutput::Silent : AudioOutput::Device );
        AudioThread->SetOcclusionQuery( [this]( const Vector3& InListener, const Vector3& InSource, Entity* InListenerEntity, Entity& InSourceEntity ) {
            return Physics ? CountAudioObstacles( *Physics, InListener, InSource, InListenerEntity, InSourceEntity ) : 0;
        } );
#if USING( ME_SCRIPTING )
        Scripts = new ScriptCore();
#endif
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
        GameWorld->AddCore<ParticleCore>( *Particles );
        GameWorld->AddCore<PhysicsCore>( *Physics );
        GameWorld->AddCore<Physics2DCore>( *Physics2D );
        GameWorld->AddCore<AnimationCore>( *Animation );
        GameWorld->AddCore<NavigationCore>( *Navigation );
        GameWorld->AddCore<AudioCore>( *AudioThread );
        GameWorld->AddCore<UICore>( *UI );
        if( Scripts )
        {
            GameWorld->AddCore<ScriptCore>( *Scripts );
        }
    }

    // The project's action map (a game can load another in OnInitialize).
    const std::string& actions = ProjectSettings::Get().InputActions;
    if( !actions.empty() && Path( actions ).Exists )
    {
        m_input.LoadActions( Path( actions ) );
    }

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
#if USING( ME_SCRIPTING )
        ScriptEngine::PollReload();
#endif
#endif

        // Frame timing: clamp hitches (debugger breaks, loading) so the simulation never tries to
        // catch up on seconds of backlog, then apply pause / time scale.
        float frameSeconds = 0.f;
        float scaledSeconds = 0.f;
        {
            OPTICK_EVENT( "Clock" );
            GameClock.Update();
            frameSeconds = m_fixedFrameDelta > 0.f ? m_fixedFrameDelta : static_cast<float>( std::min( GameClock.GetDeltaSecondsPrecise(), static_cast<double>( m_maxFrameDelta ) ) );
            if( m_automation.IsCaptureFrozen() )
            {
                // The requested screenshot arrives a frame or more later: hold time still until then,
                // so the capture is the frame that was asked for, whatever the GPU's latency.
                frameSeconds = 0.f;
            }
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
            UnscaledDeltaTime = frameSeconds;
            updateContext.FixedDeltaTime = m_fixedTimeStep;
            updateContext.BeginFrame( scaledSeconds, frameSeconds, m_timeScale, m_isPaused );
        }

        GetInput().Update();
#if USING( ME_EDITOR )
        GetEditorInput().Update();
#endif

        {
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
                ME_STAT_SCOPE( "Fixed Update" );
                m_fixedAccumulator += scaledSeconds;
                int steps = 0;
                while( m_fixedAccumulator >= m_fixedTimeStep && steps < m_maxFixedStepsPerFrame )
                {
                    updateContext.IsFixedStepActive = true;
                    GameWorld->FixedUpdateLoadedCores( updateContext );
                    if( Scripts )
                    {
                        Scripts->FixedUpdate( updateContext );
                    }
                    m_game->OnFixedUpdate( updateContext );
                    // Gameplay applied its forces; now the world steps.
                    Physics->FixedUpdate( updateContext );
                    Physics2D->FixedUpdate( updateContext );
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

                // Interpolated body poses land before gameplay reads them (cameras following bodies).
                Physics->Update( updateContext );
                Physics2D->Update( updateContext );
            }

            // Update Loaded Cores
            {
                ME_FRAMEPROFILE_SCOPED( "Game Cores", ProfileCategory::Game );
                ME_STAT_SCOPE( "Scene Cores" );
                GameWorld->UpdateLoadedCores( updateContext );
                if( Scripts )
                {
                    Scripts->Update( updateContext );
                }
                GameWorld->Simulate();
            }

            // Update Cameras
            {
                OPTICK_EVENT( "SceneNodes->Update" );
                ME_FRAMEPROFILE_SCOPED( "SceneNodes", ProfileCategory::UI );
                ME_STAT_SCOPE( "Scene Graph" );
                SceneNodes->Update( updateContext );
            }

            // Update Game Application
            {
                ME_FRAMEPROFILE_SCOPED( "Game", ProfileCategory::Game );
                ME_STAT_SCOPE( "Game Update" );
                OPTICK_CATEGORY( "MainLoop::GameUpdate", Optick::Category::GameLogic );
                m_game->OnUpdate( updateContext );
                GameWorld->Simulate();
            }

            // Navigation (after gameplay set destinations, before animation reads agent motion)
            {
                ME_FRAMEPROFILE_SCOPED( "Navigation", ProfileCategory::Game );
                ME_STAT_SCOPE( "Navigation" );
                Navigation->Update( updateContext );
            }

            // Animation (after gameplay set its parameters, before render prep skins meshes)
            {
                ME_FRAMEPROFILE_SCOPED( "Animation", ProfileCategory::Game );
                ME_STAT_SCOPE( "Animation" );
                Animation->Update( updateContext );
            }

            // Audio (after gameplay and animation moved this frame's listener and sources)
            {
                ME_STAT_SCOPE( "Audio" );
                AudioThread->Update( updateContext );
            }

            // Particles (simulated before render prep so this frame's particles draw)
            {
                ME_FRAMEPROFILE_SCOPED( "Particles", ProfileCategory::Rendering );
                ME_STAT_SCOPE( "Particles" );
                Particles->Update( updateContext );
            }

            // Model Renderer Update
            {
                ME_FRAMEPROFILE_SCOPED( "ModelRenderer", ProfileCategory::Rendering );
                ME_STAT_SCOPE( "Render Prep" );
                ModelRenderer->Update( updateContext );
            }

            // UI Update
            {
                OPTICK_CATEGORY( "UICore::Update", Optick::Category::Rendering )
                ME_FRAMEPROFILE_SCOPED( "UI", ProfileCategory::UI );
                ME_STAT_SCOPE( "UI Update" );
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

            // Finish background loads (GPU uploads) before this frame's render commands are built.
            // Deterministic runs (--frame-time) wait for them, so captures never depend on IO timing.
            if( m_fixedFrameDelta > 0.f )
            {
                ResourceCache::GetInstance().WaitForAsyncLoads();
            }
            else
            {
                ResourceCache::GetInstance().PumpAsyncLoads();
            }
            Model::ExpandPendingModels();

            // Late Update
            {
                OPTICK_EVENT( "LateUpdate" );
                ME_STAT_SCOPE( "Late Update" );
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
                {
                    ME_STAT_SCOPE( "UI Render" );
                    UI->Render();
                }
                ME_FRAMEPROFILE_STOP( "UI Render" );
                ME_FRAMEPROFILE_START( "Render", ProfileCategory::Rendering );
                {
                    ME_STAT_SCOPE( "Render Submit" );
                    NewRenderer->Render( EditorCamera, m_frameRenderSettings );
                }
                ME_FRAMEPROFILE_STOP( "Render" );
                DebugDraw::EndFrame( updateContext.GetUnscaledDeltaTime() );
                UI->PostRender( updateContext );
                m_game->PostRender();
            }
            FrameStats::Get().EndFrame( std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - frameStartTime ).count() );

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
        m_automation.Shutdown();
    }
    // Unattended runs must not clobber the user's window config.
    if( !AutomationRunner::IsUnattendedRun() )
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
    AssetsChangedEvent changedEvent;
    for( const FileWatcher::Change& change : changes )
    {
        changedEvent.Paths.push_back( change.FullPath );
        if( change.Type != FileWatcher::ChangeType::Removed )
        {
            paths.push_back( change.FullPath );
        }
        if( change.FullPath.size() > 7 && change.FullPath.compare( change.FullPath.size() - 7, 7, ".prefab" ) == 0 )
        {
            SceneSerializer::ClearPrefabCache();
        }
#if USING( ME_SCRIPTING )
        // Game scripts: rebuild in the background and hot reload them.
        if( change.FullPath.size() > 3 && change.FullPath.compare( change.FullPath.size() - 3, 3, ".cs" ) == 0 )
        {
            ScriptEngine::RequestReload();
        }
#endif
    }
    // Editing a shader include or varying file reloads every shader built from it.
    Moonlight::ExpandShaderChanges( paths );
    for( const std::string& reloaded : ResourceCache::GetInstance().OnFilesChanged( paths ) )
    {
        CLog::Log( CLog::LogType::Info, "Hot reloaded: " + reloaded );
    }
    changedEvent.Fire();
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
    if( AudioThread )
    {
        AudioThread->Shutdown();
    }
#if USING( ME_SCRIPTING )
    ScriptEngine::Shutdown();
#endif
    Gamepads::Get().CloseAll();

    // GPU teardown: drop the cached assets, then the renderer and bgfx. GPU objects released after
    // this (static caches, the game object) skip their handles (Moonlight::IsGpuAlive).
    ResourceCache::GetInstance().ReleaseAll();
    if( NewRenderer )
    {
        NewRenderer->Destroy();
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
