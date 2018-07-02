#include "MitchGame.h"
#include "Cores/Renderer.h"
#include "Utility/Logger.h"
#include "Components/Sprite.h"
#include "Engine/Component.h"
#include "Components/Transform.h"
#include "Engine/Entity.h"
#include <string>
#include "Engine/Input.h"
#include "Engine/Window.h"
#include "Components/Animation.h"
#include "Components/Camera.h"
#include "Components/FlyingCamera.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Debug/DebugCube.h"

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
	Transform& CameraPos = MainCamera.GetComponent<Transform>();
	CameraPos.Position = glm::vec3(0, 5, 20);
	MainCamera.AddComponent<Camera>();
	MainCamera.AddComponent<FlyingCamera>();

	SecondaryCamera = GameWorld->CreateEntity();
	Transform& SecondaryPos = SecondaryCamera.GetComponent<Transform>();
	SecondaryPos.Position = glm::vec3(0, 5, 20);
	SecondaryCamera.AddComponent<Camera>();
	SecondaryCamera.AddComponent<FlyingCamera>();

	glm::vec3 cubePositions[] = {
		glm::vec3(0.0f,  0.0f,  0.0f),
		glm::vec3(2.0f,  5.0f, -15.0f),
		glm::vec3(-1.5f, -2.2f, -2.5f),
		glm::vec3(-3.8f, -2.0f, -12.3f),
		glm::vec3(2.4f, -0.4f, -3.5f),
		glm::vec3(-1.7f,  3.0f, -7.5f),
		glm::vec3(1.3f, -2.0f, -2.5f),
		glm::vec3(1.5f,  2.0f, -2.5f),
		glm::vec3(1.5f,  0.2f, -1.5f),
		glm::vec3(-1.3f,  1.0f, -1.5f)
	};

	for (auto cubePosition : cubePositions)
	{
		Entity Cube = GameWorld->CreateEntity();
		Transform& BGPos = Cube.GetComponent<Transform>();
		BGPos.Position = cubePosition;
		Sprite& BGSprite = Cube.AddComponent<Sprite>();
		Cube.AddComponent<DebugCube>();
		BGSprite.SetImage(Resources.Get<Texture>("Assets/colored_grass.png"));
		Cubes.push_back(Cube);
	}
	FlyingCameraController = new FlyingCameraCore();
	GameWorld->AddCore<FlyingCameraCore>(*FlyingCameraController);
}

void MitchGame::Update(float DeltaTime)
{
	FlyingCameraController->Update(DeltaTime);

	Input& Instance = Input::Get();
	if (Instance.IsKeyDown(GLFW_KEY_1))
	{
		Camera::CurrentCamera = &MainCamera.GetComponent<Camera>();
	}
	if (Instance.IsKeyDown(GLFW_KEY_2))
	{
		Camera::CurrentCamera = &SecondaryCamera.GetComponent<Camera>();
	}
	if (Instance.IsKeyDown(GLFW_KEY_P) && !AddedPhysics)
	{
		for (auto& Cube : Cubes)
		{
			Cube.AddComponent<Rigidbody>();
		}
		AddedPhysics = true;
	}

	Transform& TransformComponent = MainCamera.GetComponent<Transform>();
}

void MitchGame::End()
{
}