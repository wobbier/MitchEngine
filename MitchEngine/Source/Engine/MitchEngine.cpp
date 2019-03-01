#include "PCH.h"
#include "MitchEngine.h"
#include "Logger.h"
#include "Config.h"
#include "Window/UWPWindow.h"
#include "Window/D3D12Window.h"
#include "Events/EventManager.h"
#include "Cores/PhysicsCore.h"
#include "Cores/Camera/CameraCore.h"
#include "Cores/SceneGraph.h"
#include "Cores/Rendering/RenderCore.h"
#include "Game.h"
#include "Window/IWindow.h"
#include "Input.h"
#include "JobScheduler.h"
#include <thread>

struct UpdatePhysicsParam
{
	float DeltaTime;
	PhysicsCore* Core;
};
void UpdatePhysicsCore(JobScheduler* manager, void* game)
{
	UpdatePhysicsParam* physics = reinterpret_cast<UpdatePhysicsParam*>(game);
	physics->Core->Update(physics->DeltaTime);
}

MitchEngine::MitchEngine()
	: Running(true)
	, GameClock(Clock::GetInstance())
{
}

MitchEngine::~MitchEngine()
{

}

void MitchEngine::Init(Game* game)
{
	if (m_isInitialized || !game)
	{
		return;
	}

	m_game = game;

	Logger::GetInstance().SetLogFile("engine.txt");
	Logger::GetInstance().SetLogPriority(Logger::LogType::Info);
	Logger::GetInstance().Log(Logger::LogType::Info, "Starting the MitchEngine.");

	EngineConfig = new Config("Assets\\Config\\Engine.cfg");

	//auto WindowConfig = EngineConfig->Root["window"];
	int WindowWidth = 1280;//WindowConfig["width"].asInt();
	int WindowHeight = 720;//WindowConfig["height"].asInt();

#if ME_PLATFORM_WIN64
	GameWindow = new D3D12Window("MitchEngine", WindowWidth, WindowHeight);
#endif
#if ME_PLATFORM_UWP
	GameWindow = new UWPWindow("MitchEngine", WindowWidth, WindowHeight);
#endif

	m_renderer = new Moonlight::Renderer();
	m_renderer->WindowResized(GameWindow->GetSize());

	GameWorld = std::make_shared<World>();

	Physics = new PhysicsCore();
	GameWorld->AddCore<PhysicsCore>(*Physics);

	Cameras = new CameraCore();
	GameWorld->AddCore<CameraCore>(*Cameras);

	SceneNodes = new SceneGraph();
	GameWorld->AddCore<SceneGraph>(*SceneNodes);

	ModelRenderer = new RenderCore();
	GameWorld->AddCore<RenderCore>(*ModelRenderer);

	m_game->Initialize();

	m_isInitialized = true;
}

void MitchEngine::Run(JobScheduler* scheduler)
{
	m_renderer->Init();
	GameClock.Reset();
	float PreviousDeltaTime = 0.0f;
	// Game loop
	forever
	{
		BROFILER_FRAME("MainLoop");
	// Check and call events
		GameWindow->ParseMessageQueue();

	if (GameWindow->ShouldClose())
	{
		m_game->End();
		return;
	}

		EventManager::GetInstance().FirePendingEvents();

		float time = GameClock.GetTimeInMilliseconds();
		float deltaTime = GameClock.deltaTime = ((time <= 0.0f || time >= 0.5f) ? 0.0001f : time) + PreviousDeltaTime;

		AccumulatedTime += deltaTime;
		AccumulatedFrameTime += deltaTime;
		//while (AccumulatedTime >= 1.0f / EngineFrequency)
		{

			// Update our engine
			GameWorld->Simulate();

			{
				BROFILER_CATEGORY("MainLoop::GameUpdate", Brofiler::Color::CornflowerBlue);
				m_game->Update(deltaTime);
			}

			//Physics->Update(deltaTime);
			AtomicCounter counter(scheduler);
			UpdatePhysicsParam param;
			param.Core = Physics;
			param.DeltaTime = (1.0f / EngineFrequency);
			Job job{ UpdatePhysicsCore, &param };
			scheduler->AddSigleJob(job, &counter);

			scheduler->WaitForCounter(&counter, 0, true);

			SceneNodes->Update(deltaTime);

			Cameras->Update(deltaTime);

			if (AccumulatedTime > 1.0f / FPS)
			{
				ModelRenderer->Update(AccumulatedTime);
				m_renderer->Render();
				AccumulatedTime -= 1.0f / FPS;
			}

			{
				float now = GameClock.GetTimeInMilliseconds();
				float newDelta = (now <= 0.0f || now >= 0.3) ? 0.0001f : now;

				BROFILER_CATEGORY("Thread::Sleep", Brofiler::Color::AliceBlue);
				std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(((1.0f / EngineFrequency) - newDelta) * 1000.0f)));// + (deltaTime - PreviousDeltaTime)

				PreviousDeltaTime = newDelta;
			}
		}
	}
}

Moonlight::Renderer& MitchEngine::GetRenderer() const
{
	return *m_renderer;
}

std::weak_ptr<World> MitchEngine::GetWorld() const
{
	return GameWorld;
}

const bool MitchEngine::IsInitialized() const
{
	return m_isInitialized;
}

bool MitchEngine::IsRunning() const
{
	return true;
}

void MitchEngine::Quit() { Running = false; }

IWindow* MitchEngine::GetWindow()
{
	return GameWindow;
}
