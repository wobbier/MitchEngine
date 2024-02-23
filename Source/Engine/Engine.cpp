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
#include "Events/PlatformEvents.h"
#include "Scripting/ScriptEngine.h"
#include "Core/Assert.h"

Engine& GetEngine()
{
    return Engine::GetInstance();
}

Engine::Engine()
    : Running( true )
    , newJobSystem( 1, 100000 )
    , m_jobSystem( 4 )
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
extern bool ImGui_ImplWin32_Init( void* window );
void Engine::Init( Game* game )
{
    if( m_isInitialized || !game )
    {
        return;
    }

    m_game = game;

    CLog::GetInstance().SetLogFile( "Engine.txt" );
    CLog::GetInstance().SetLogVerbosity( CLog::LogType::Info );
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

    engineConfig = EngineConfig( engineCfg );
    engineConfig.OnLoadConfig( engineConfig.Root );
#if USING( ME_PLATFORM_WIN64 )
    GameWindow = new SDLWindow( engineConfig.GetValue( "Title" ), ResizeFunc, engineConfig.WindowPosition.x, engineConfig.WindowPosition.y, engineConfig.WindowSize );
#endif

#if USING( ME_PLATFORM_MACOS )
    int WindowWidth = 1280;
    int WindowHeight = 720;
    GameWindow = new SDLWindow( "MitchEngine", ResizeFunc, 500, 300, Vector2( WindowWidth, WindowHeight ) );
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

    NewRenderer = new BGFXRenderer();
    RendererCreationSettings settings;
    settings.WindowPtr = GameWindow->GetWindowPtr();
    settings.InitialSize = engineConfig.WindowSize;
    NewRenderer->Create( settings );
#if USING( ME_IMGUI )
#if USING( ME_PLATFORM_WIN64 )
    ImGui_ImplSDL2_InitForD3D( static_cast<SDLWindow*>( GameWindow )->WindowHandle );
#endif
#if USING( ME_PLATFORM_MACOS )
    ImGui_ImplSDL2_InitForMetal( static_cast<SDLWindow*>( GameWindow )->WindowHandle );
#endif
#endif
    //m_renderer = new Moonlight::Renderer();
    //m_renderer->WindowResized(GameWindow->GetSize());

    GameWorld = MakeShared<World>();

    Cameras = new CameraCore();

    SceneNodes = new SceneCore();

    ModelRenderer = new RenderCore();
    AudioThread = new AudioCore();

    //m_renderer->Init();

    UI = new UICore( GameWindow, NewRenderer );

    NewRenderer->SetGuizmoDrawCallback( [this]( DebugDrawer* drawer )
        {
            std::vector<BaseCore*> cores = GetWorld().lock()->GetAllCores();
            for( BaseCore* core : cores )
            {
                core->OnDrawGuizmo( drawer );
            }
        } );

    InitGame();

    ResizeFunc( engineConfig.WindowSize );

    // Register systems
    {
        systemRegistry.RegisterSystem( NewRenderer );
    }
    updateContext.m_SystemRegistry = &systemRegistry;

    m_isInitialized = true;
}

void Engine::InitGame()
{
    GameWorld->AddCore<CameraCore>( *Cameras );
    GameWorld->AddCore<SceneCore>( *SceneNodes );
    GameWorld->AddCore<RenderCore>( *ModelRenderer );
    GameWorld->AddCore<AudioCore>( *AudioThread );
    GameWorld->AddCore<UICore>( *UI );

    m_game->OnInitialize();
}

void Engine::StopGame()
{
    m_game->OnEnd();
}

void Engine::Run()
{
    m_game->OnStart();

    GameClock.Reset();

    const float FramesPerSec = FPS;
    const float MaxDeltaTime = ( 1.f / FramesPerSec );

    // Game loop
    forever
    {
        OPTICK_FRAME( "MainLoop" );
#if USING( ME_BASIC_PROFILER )
        FrameProfile::GetInstance().Start();
#endif
        // Check and call events
        GameWindow->ParseMessageQueue();

        if( GameWindow->ShouldClose() )
        {
            StopGame();
            break;
        }

        {
            OPTICK_EVENT( "EventManager", Optick::Category::Cloth );
            EventManager::GetInstance().FirePendingEvents();
        }

        {
            OPTICK_EVENT( "Clock" );
            GameClock.Update();

            AccumulatedTime += GameClock.GetDeltaSeconds();
        }

        //if (AccumulatedTime >= MaxDeltaTime)
        {
            float deltaTime = DeltaTime = AccumulatedTime;
            updateContext.UpdateDeltaTime( deltaTime );

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

                NewRenderer->BeginFrame( mousePos, ( input.IsMouseButtonDown( MouseButton::Left ) ? 0x01 : 0 )
                    | ( input.IsMouseButtonDown( MouseButton::Right ) ? 0x02 : 0 )
                    | ( input.IsMouseButtonDown( MouseButton::Middle ) ? 0x04 : 0 )
                    , (int32_t)input.GetMouseScrollOffset().y
                    , GameWindow->GetSize()
                    , -1
                    , 255 );
            }
#endif

            GameWorld->Simulate();

            GetInput().Update();
#if USING( ME_EDITOR )
            GetEditorInput().Update();
#endif

            // Update Loaded Cores
            {
                // TODO: This is wrong?? There's more than just physics in loaded cores...
                ME_FRAMEPROFILE_SCOPED( "Physics", ProfileCategory::Physics );
                GameWorld->UpdateLoadedCores( updateContext );
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
                GameWorld->LateUpdateLoadedCores( updateContext );
                Cameras->Update( updateContext );
                SceneNodes->LateUpdate( updateContext );
                Cameras->LateUpdate( updateContext );
                AudioThread->LateUpdate( updateContext );
                ModelRenderer->LateUpdate( updateContext );
                UI->LateUpdate( updateContext );
            }

            // Render
            {
                m_game->PostRender();
#if !USING( ME_EDITOR )
                EditorCamera.OutputSize = GetWindow()->GetSize();
#if USING( ME_BASIC_PROFILER )
                FrameProfile::GetInstance().Render( { 10.f, ( GameWindow->GetSize().y - FrameProfile::kMinProfilerSize ) - 10 }, { GameWindow->GetSize().x - 20, (float)FrameProfile::kMinProfilerSize } );
#endif
#endif
                ME_FRAMEPROFILE_START( "UI Render", ProfileCategory::UI );
                UI->Render();
                ME_FRAMEPROFILE_STOP( "UI Render" );
                ME_FRAMEPROFILE_START( "Render", ProfileCategory::Rendering );
                NewRenderer->Render( EditorCamera );
                ME_FRAMEPROFILE_STOP( "Render" );
            }


#if USING( ME_BASIC_PROFILER )
            // This makes the profiler overview data to be delayed for a frame, but takes the renderer into account.
            {
                static float fpsTime = 0;
                fpsTime += AccumulatedTime;
                if( fpsTime > 1.f )
                {
                    FrameProfile::GetInstance().Dump();
                    fpsTime -= 1.f;
                }
            }
#endif

            AccumulatedTime = std::fmod( AccumulatedTime, MaxDeltaTime );

#if USING( ME_BASIC_PROFILER )
            FrameProfile::GetInstance().End( AccumulatedTime );
#endif
            AccumulatedTime = 0;// std::fmod(AccumulatedTime, MaxDeltaTime);
            GetJobEngine().ClearWorkerPools();
            GetInput().PostUpdate();
#if USING ( ME_EDITOR )
            GetEditorInput().PostUpdate();
#endif
        }
        ResourceCache::GetInstance().Dump();
        //Sleep(1);
    }
    engineConfig.Save();
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

void Engine::Quit()
{
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

JobEngine& Engine::GetJobEngine()
{
    return newJobSystem;
}

std::tuple<Worker*, Pool&> Engine::GetJobSystemNew()
{
    return { newJobSystem.GetThreadWorker(), newJobSystem.GetThreadWorker()->GetPool() };
}

void Engine::LoadScene( const std::string& SceneFile )
{
    Cameras->Init();
    if( CurrentScene )
    {
        CurrentScene->UnLoad();
        delete CurrentScene;
        CurrentScene = nullptr;
    }

    GameWorld->Unload();
    SceneNodes->Init();
    CurrentScene = new Scene( SceneFile );

    if( !CurrentScene->Load( GameWorld ) && !CurrentScene->IsNewScene() )
    {
        ME_ASSERT_MSG( false, "Failed to load scene." );
    }

#if USING( ME_SCRIPTING )
    ScriptEngine::sScriptData.worldPtr = GetWorld();
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
