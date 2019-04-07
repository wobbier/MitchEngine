#include "PCH.h"
#include "Game.h"
#include "Logger.h"
#include "Engine/Clock.h"
#include "Cores/Rendering/RenderCore.h"
#include "Cores/Camera/CameraCore.h"
#include "Cores/PhysicsCore.h"
#include "Cores/SceneGraph.h"
#include "Graphics/Cubemap.h"
#include "Engine/Input.h"
#include "Events/EventManager.h"

#include "Window/D3D12Window.h"
#include "Window/UWPWindow.h"

Engine Game::m_engine;

Game::Game()
{
}

Game::~Game()
{
}


void Game::Run()
{
	GetEngine().Init(this);
	GetEngine().Run();
}

void Game::OnInitialize()
{

}

void Game::OnStart()
{

}

void Game::OnEnd()
{

}


void Game::OnUpdate(float DeltaTime)
{
}