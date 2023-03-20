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

Engine& GetEngine()
{
	return Engine::GetInstance();
}

Engine::Engine()
	: Running( true )
	, newJobSystem(1, 100000)
{
	std::vector<TypeId> events;
	events.push_back( LoadSceneEvent::GetEventId() );
	EventManager::GetInstance().RegisterReceiver( this, events );
}

Engine::~Engine()
{
	delete engineConfig;
}

extern bool ImGui_ImplSDL2_InitForD3D( SDL_Window* window );
extern bool ImGui_ImplSDL2_InitForMetal( SDL_Window* window );
extern bool ImGui_ImplWin32_Init( void* window );
void Engine::Init( Game* game )
{
	if ( m_isInitialized || !game )
	{
		return;
	}

	m_game = game;

	CLog::GetInstance().SetLogFile( "Engine.txt" );
	CLog::GetInstance().SetLogVerbosity( CLog::LogType::Info );
	CLog::GetInstance().Log( CLog::LogType::Info, "Starting the MitchEngine." );
	Path engineCfg( "Assets\\Config\\Engine.cfg" );

#if USING( ME_EDITOR )
	if ( engineCfg.FullPath.rfind( "Engine" ) != -1 )
	{
		Path gameEngineCfgPath( "Assets\\Config\\Engine.cfg", true );
		if ( !gameEngineCfgPath.Exists )
		{
#if USING( ME_PLATFORM_WIN64 )
			CreateDirectory( StringUtils::ToWString( gameEngineCfgPath.Directory ).c_str(), NULL );
#endif
			File gameEngineCfg = File( engineCfg );
			File newGameConfig( gameEngineCfgPath );
			newGameConfig.Write( gameEngineCfg.Read() );
		}
	}
#endif


	std::function<void( const Vector2& )> ResizeFunc = [this] ( const Vector2& NewSize )
	{
		if ( NewRenderer )
		{
			NewRenderer->WindowResized( NewSize );
		}

		if ( UI )
		{
			if ( Camera::CurrentCamera )
			{
				UI->OnResize( Camera::CurrentCamera->OutputSize );
			}
		}
		WindowResizedEvent evt;
		evt.NewSize = NewSize;
		evt.Fire();
	};

#if USING( ME_PLATFORM_WIN64 )
	engineConfig = new EngineConfig( engineCfg );
	engineConfig->OnLoad( engineConfig->Root );
	const json& WindowConfig = engineConfig->GetJsonObject( "Window" );
	int WindowWidth = WindowConfig["Width"];
	int WindowHeight = WindowConfig["Height"];
	GameWindow = new SDLWindow( engineConfig->GetValue( "Title" ), ResizeFunc, 500, 300, engineConfig->WindowSize );
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

	ResizeFunc( Vector2( 1920, 1080 ) );

	NewRenderer = new BGFXRenderer();
	RendererCreationSettings settings;
	settings.WindowPtr = GameWindow->GetWindowPtr();
	settings.InitialSize = Vector2( 1920.f, 1080.f );
	NewRenderer->Create( settings );

#if USING( ME_PLATFORM_WIN64 )
	ImGui_ImplSDL2_InitForD3D( static_cast<SDLWindow*>( GameWindow )->WindowHandle );
#endif
#if USING( ME_PLATFORM_MACOS )
	ImGui_ImplSDL2_InitForMetal( static_cast<SDLWindow*>( GameWindow )->WindowHandle );
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

	NewRenderer->SetGuizmoDrawCallback( [this] ( DebugDrawer* drawer )
	{
		std::vector<BaseCore*> cores = GetWorld().lock()->GetAllCores();
		for ( BaseCore* core : cores )
		{
			core->OnDrawGuizmo( drawer );
		}
	} );

	InitGame();

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
		FrameProfile::GetInstance().Start();
		// Check and call events
		GameWindow->ParseMessageQueue();

		if ( GameWindow->ShouldClose() )
		{
			StopGame();
			break;
		}

		EventManager::GetInstance().FirePendingEvents();

		GameClock.Update();

		AccumulatedTime += GameClock.GetDeltaSeconds();

		//if (AccumulatedTime >= MaxDeltaTime)
		{
			OPTICK_FRAME( "MainLoop" );
			float deltaTime = DeltaTime = AccumulatedTime;
			updateContext.UpdateDeltaTime( deltaTime );
			{
#if USING( ME_EDITOR )
				Input& input = GetEditorInput();
#else
				Input& input = GetInput();
#endif

				ImGuiIO& io = ImGui::GetIO();
				Vector2 mousePos = input.GetMousePosition();
				if ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
				{
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

			GameWorld->Simulate();

			GetInput().Update();

			// Update Loaded Cores
			{
				FrameProfile::GetInstance().Set( "Physics", ProfileCategory::Physics );
				GameWorld->UpdateLoadedCores( updateContext );
				FrameProfile::GetInstance().Complete( "Physics" );
			}

			// Update Cameras
			{
				FrameProfile::GetInstance().Set( "SceneNodes", ProfileCategory::UI );
				SceneNodes->Update( updateContext );
				FrameProfile::GetInstance().Complete( "SceneNodes" );
			}

			// Update Game Application
			{
				FrameProfile::GetInstance().Set( "Game", ProfileCategory::Game );
				//ME_PROFILE("Game", ProfileCategory::Game);
				OPTICK_CATEGORY( "MainLoop::GameUpdate", Optick::Category::GameLogic );
				m_game->OnUpdate( updateContext );
				FrameProfile::GetInstance().Complete( "Game" );
			}

			// Update Audio
			{

				AudioThread->Update( deltaTime );
			}

			// Model Renderer Update
			{
				FrameProfile::GetInstance().Set( "ModelRenderer", ProfileCategory::Rendering );
				ModelRenderer->Update( updateContext );
				FrameProfile::GetInstance().Complete( "ModelRenderer" );
			}

			// UI Update
			{
				OPTICK_CATEGORY( "UICore::Update", Optick::Category::Rendering )
				FrameProfile::GetInstance().Set( "UI", ProfileCategory::UI );
				// editor only?
				if ( UI )
				{
					if ( Camera::CurrentCamera )
					{
						UI->OnResize( Camera::CurrentCamera->OutputSize );
					}
					UI->Update( updateContext );
				}
				FrameProfile::GetInstance().Complete( "UI" );
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
#if _DEBUG
				FrameProfile::GetInstance().Render( { 0.f, GameWindow->GetSize().y - FrameProfile::kMinProfilerSize }, { GameWindow->GetSize().x, (float)FrameProfile::kMinProfilerSize } );
#endif
#endif
				FrameProfile::GetInstance().Set( "UI Render", ProfileCategory::UI );
				UI->Render();
				FrameProfile::GetInstance().Complete( "UI Render" );
				FrameProfile::GetInstance().Set( "Render", ProfileCategory::Rendering );
				NewRenderer->Render( EditorCamera );
				FrameProfile::GetInstance().Complete( "Render" );
			}

			// This makes the profiler overview data to be delayed for a frame, but takes the renderer into account.
			{
				static float fpsTime = 0;
				fpsTime += AccumulatedTime;
				if ( fpsTime > 1.f )
				{
					FrameProfile::GetInstance().Dump();
					fpsTime -= 1.f;
				}
			}

			AccumulatedTime = std::fmod( AccumulatedTime, MaxDeltaTime );

			FrameProfile::GetInstance().End( AccumulatedTime );
			AccumulatedTime = 0;// std::fmod(AccumulatedTime, MaxDeltaTime);
			GetJobEngine().ClearWorkerPools();
			GetInput().PostUpdate();
		}
		ResourceCache::GetInstance().Dump();
		//Sleep(1);
	}
	engineConfig->Save();
}

bool Engine::OnEvent( const BaseEvent& evt )
{
	if ( evt.GetEventId() == LoadSceneEvent::GetEventId() )
	{
		const LoadSceneEvent& test = static_cast<const LoadSceneEvent&>( evt );
		//InputEnabled = test.Enabled;
		LoadScene( test.Level );
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

EngineConfig& Engine::GetConfig() const
{
	return *engineConfig;
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
	if ( CurrentScene )
	{
		CurrentScene->UnLoad();
		delete CurrentScene;
		CurrentScene = nullptr;
	}

	GameWorld->Unload();
	SceneNodes->Init();
	CurrentScene = new Scene( SceneFile );

	if ( !CurrentScene->Load( GameWorld ) )
	{
	}

	SceneLoadedEvent evt;
	evt.LoadedScene = CurrentScene;
	evt.Fire();
	ScriptEngine::sScriptData.worldPtr = GetWorld();
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
