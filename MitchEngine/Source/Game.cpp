#include "PCH.h"
#include "Game.h"
#include "Utility/Logger.h"
#include "Engine/Clock.h"
#include "Cores/Rendering/Renderer.h"
#include "Cores/Rendering/DifferedLighting.h"
#include "Cores/Camera/CameraCore.h"
#include "Cores/AnimationCore.h"
#include "Cores/PhysicsCore.h"
#include "Cores/SceneGraph.h"
#include "Graphics/Cubemap.h"
#include "Engine/Input.h"
#ifdef MAN_EDITOR
#include "Graphics/UI/imgui.h"
#include "Graphics/UI/imgui_impl_glfw.h"
#include "Graphics/UI/imgui_impl_opengl3.h"
#endif

Game::Game()
	: Running(true)
	, GameClock(Clock::GetInstance())
{
}

Game::~Game()
{
}
bool my_tool_active = false;
#ifdef ME_PLATFORM_UWP
void Game::Start(const std::shared_ptr<DX::DeviceResources>& deviceResources)
#else
void Game::Start()
#endif
{
	Logger::GetInstance().SetLogFile("engine.txt");
	Logger::GetInstance().SetLogPriority(Logger::LogType::Info);
	Logger::GetInstance().Log(Logger::LogType::Info, "Starting the MitchEngine.");

	EngineConfig = new Config("Assets\\Config\\Engine.cfg");

	//auto WindowConfig = EngineConfig->Root["window"];
	int WindowWidth = 1280;//WindowConfig["width"].asInt();
	int WindowHeight = 720;//WindowConfig["height"].asInt();

#ifdef ME_PLATFORM_WIN64
	GameWindow = new Window("MitchEngine", WindowWidth, WindowHeight);
#endif

	GameWorld = new World();

	//auto ModelRenderer = Renderer();
	//GameWorld->AddCore<Renderer>(ModelRenderer);

	//auto LightingRenderer = DifferedLighting();
	//GameWorld->AddCore<DifferedLighting>(LightingRenderer);

	Animator = new AnimationCore();
	GameWorld->AddCore<AnimationCore>(*Animator);

	Physics = new PhysicsCore();
	GameWorld->AddCore<PhysicsCore>(*Physics);

	Cameras = new CameraCore();
	GameWorld->AddCore<CameraCore>(*Cameras);

	SceneNodes = new SceneGraph();
	GameWorld->AddCore<SceneGraph>(*SceneNodes);

#ifdef ME_PLATFORM_UWP
	ModelRenderer = new Renderer(deviceResources);
	GameWorld->AddCore<Renderer>(*ModelRenderer);
#endif

	Initialize();

	GameClock.Reset();
}

void Game::Tick()
{
	int i = 0;
#ifdef MAN_EDITOR
	i = 2;
#endif

#ifdef MAN_EDITOR
	bool show_demo_window = true;
	bool show_another_window = false;
	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
#endif

	// Game loop
#ifdef ME_PLATFORM_WIN64
	while (!GameWindow->ShouldClose())
	{
		//BROFILER_FRAME("MainLoop")
			// Check and call events
			GameWindow->PollInput();
#endif

		float time = GameClock.GetTimeInMilliseconds();
		const float deltaTime = GameClock.deltaTime = (time <= 0.0f || time >= 0.3) ? 0.0001f : time;

#ifdef MAN_EDITOR
		/*
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		*/
#endif

		// Update our engine
		GameWorld->Simulate();
		Physics->Update(deltaTime);
		Animator->Update(deltaTime);

		Update(deltaTime);

		SceneNodes->Update(deltaTime);

		Cameras->Update(deltaTime);

#ifdef ME_PLATFORM_UWP
		ModelRenderer->Update(deltaTime);
#endif
		//LightingRenderer.PreRender();
		//ModelRenderer.Render();
		//LightingRenderer.PostRender();

#ifdef MAN_EDITOR
		show_demo_window = true;

		// 3. Show the ImGui demo window. Most of the sample code is in ImGui::ShowDemoWindow(). Read its code to learn more about Dear ImGui!
		if (true)
		{
			ImGui::SetNextWindowPos(ImVec2(650, 20), ImGuiCond_FirstUseEver); // Normally user code doesn't need/want to call this because positions are saved in .ini file anyway. Here we just want to make the demo initial state a bit more friendly!
			ImGui::ShowDemoWindow(&show_demo_window);
		}
		ImGui::Render();

		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif

#ifdef ME_PLATFORM_WIN64
		// Swap the buffers
		GameWindow->Swap();
	}
	glfwTerminate();
#endif
}

void Game::Initialize()
{

}

void Game::Update(float DeltaTime)
{

}

void Game::End()
{

}

bool Game::Render()
{
	return ModelRenderer->Render();
}

void Game::WindowResized()
{
	//ModelRenderer->CreateWindowSizeDependentResources();
}

bool Game::IsRunning() const
{
	return true;
}

//bool Game::IsRunning() const { return Running; }

void Game::Quit() { Running = false; }