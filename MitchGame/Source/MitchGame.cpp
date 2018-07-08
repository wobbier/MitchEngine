#include "MitchGame.h"
#include "Cores/Renderer.h"
#include "Utility/Logger.h"
#include "Components/Sprite.h"
#include "Engine/Component.h"
#include "Engine/Clock.h"
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
	Transform& CameraPos = MainCamera.AddComponent<Transform>();
	CameraPos.SetPosition(glm::vec3(0, 5, 20));
	MainCamera.AddComponent<Camera>();
	MainCamera.AddComponent<FlyingCamera>();

	SecondaryCamera = GameWorld->CreateEntity();
	Transform& SecondaryPos = SecondaryCamera.AddComponent<Transform>();
	SecondaryPos.SetPosition(glm::vec3(0, 5, 20));
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
		/*Entity Cube = GameWorld->CreateEntity();
		Transform& BGPos = Cube.AddComponent<Transform>();
		BGPos.SetPosition(cubePosition);
		BGPos.SetParent(SecondaryPos);
		Sprite& BGSprite = Cube.AddComponent<Sprite>();
		Cube.AddComponent<DebugCube>();
		BGSprite.SetImage(Resources.Get<Texture>("Assets/colored_grass.png"));*/
		//Cubes.push_back(Cube);
	}

	Transform* previousTransform = &SecondaryPos;
	for (int i = 0; i < 10; ++i)
	{
		Entity Cube = GameWorld->CreateEntity();
		Transform& BGPos = Cube.AddComponent<Transform>();
		BGPos.SetPosition(glm::vec3(i * 1.f, i * 1.5f, 0.f));
		//BGPos.SetParent(*previousTransform);
		Sprite& BGSprite = Cube.AddComponent<Sprite>();
		Cube.AddComponent<DebugCube>();
		BGSprite.SetImage(Resources.Get<Texture>("Assets/colored_grass.png"));
		Cubes.push_back(Cube);
		previousTransform = &BGPos;
		Cube.SetActive(true);
	}

	FlyingCameraController = new FlyingCameraCore();
	GameWorld->AddCore<FlyingCameraCore>(*FlyingCameraController);
}

float totalTime = 0.f;
void MitchGame::Update(float DeltaTime)
{
	FlyingCameraController->Update(DeltaTime);

	Input& Instance = Input::Get();
	if (Instance.IsKeyDown(GLFW_KEY_1))
	{
		MainCamera.GetComponent<Camera>().SetCurrent();
	}
	if (Instance.IsKeyDown(GLFW_KEY_2))
	{
		SecondaryCamera.GetComponent<Camera>().SetCurrent();
	}
	if (Instance.IsKeyDown(GLFW_KEY_P) && !AddedPhysics)
	{
		for (auto& Cube : Cubes)
		{
			Cube.AddComponent<Rigidbody>();
		}
		AddedPhysics = true;
	}
	int i = 0;
	for (auto& Cube : Cubes)
	{
		Transform& CubeTransform = Cube.GetComponent<Transform>();
		CubeTransform.SetPosition(glm::vec3(i, glm::sin(totalTime + i), 0));
		i++;
	}

	totalTime += DeltaTime;

	Camera* CurrentCamera = Camera::CurrentCamera;
	{
		if (CurrentCamera->Zoom >= 1.0f && CurrentCamera->Zoom <= 45.0f)
			CurrentCamera->Zoom -= PrevMouseScroll.y - Input::Get().GetMouseScrollOffset().y;
		if (CurrentCamera->Zoom <= 1.0f)
			CurrentCamera->Zoom = 1.0f;
		if (CurrentCamera->Zoom >= 45.0f)
			CurrentCamera->Zoom = 45.0f;
	}
	PrevMouseScroll = Input::Get().GetMouseScrollOffset();
	Transform& TransformComponent = MainCamera.GetComponent<Transform>();
}

void MitchGame::End()
{
}