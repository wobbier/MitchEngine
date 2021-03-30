#include <GameApp.h>
#include <Engine/Engine.h>
#include <Events/SceneEvents.h>

GameApp::GameApp(int argc, char** argv)
	: Game(argc, argv)
{
}

GameApp::~GameApp()
{
}

void GameApp::OnStart()
{

}

void GameApp::OnUpdate(float DeltaTime)
{

}

void GameApp::OnEnd()
{
}

void GameApp::OnInitialize()
{
	NewSceneEvent evt;
	evt.Fire();
	GetEngine().GetWorld().lock()->Start();
	GetEngine().LoadScene("Assets/Example.lvl");
	GetEngine().GetWorld().lock()->Simulate();
	GetEngine().GetWorld().lock()->Start();
}

void GameApp::PostRender()
{
}