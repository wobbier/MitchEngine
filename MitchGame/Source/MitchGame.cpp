#include "MitchGame.h"
#include "Engine/Component.h"
#include "Engine/Clock.h"
#include "Components/Transform.h"
#include "Engine/Entity.h"
#include <string>
#include "Engine/Input.h"
#include "Engine/Window.h"
#include "Components/Animation.h"
#include "Components/Camera.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Graphics/Model.h"
#include "Components/Lighting/Light.h"

#include <memory>

MitchGame::MitchGame()
	: Game()
{
}

MitchGame::~MitchGame()
{
	Game::~Game();
}

void MitchGame::Initialize()
{
	MainCamera = GameWorld->CreateEntity();
	Transform& CameraPos = MainCamera.AddComponent<Transform>("Main Camera");
	CameraPos.SetPosition(glm::vec3(0, 5, 20));
	MainCamera.AddComponent<Camera>();
	MainCamera.AddComponent<Light>();
}

void MitchGame::Update(float DeltaTime)
{
}

void MitchGame::End()
{
}