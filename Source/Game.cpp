#include "PCH.h"
#include "Game.h"
#include "Logger.h"
#include "Events/EventManager.h"

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

void Game::PostRender()
{
}
