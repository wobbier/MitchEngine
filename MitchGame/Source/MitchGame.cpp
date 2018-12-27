#include "MitchGame.h"
#include "ECS/Component.h"
#include "Engine/Clock.h"
#include "Components/Transform.h"
#include "ECS/Entity.h"
#include <string>
#include "Engine/Input.h"
#include "Engine/Window.h"
#include "Components/Animation.h"
#include "Components/Camera.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Graphics/Model.h"
#include "Components/Lighting/Light.h"
#include "Components/FlyingCamera.h"

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
	MainCamera.AddComponent<FlyingCamera>();
	MainCamera.AddComponent<Light>();

	SecondaryCamera = GameWorld->CreateEntity();
	Transform& SecondaryPos = SecondaryCamera.AddComponent<Transform>("Secondary Camera");
	SecondaryPos.SetPosition(glm::vec3(0, 5, 20));
	SecondaryCamera.AddComponent<Camera>();
	SecondaryCamera.AddComponent<Light>();
	SecondaryCamera.AddComponent<FlyingCamera>();

	Entity Ground = GameWorld->CreateEntity();
	Ground.AddComponent<Transform>("Ground obvs");
	Ground.AddComponent<Model>("Assets/ground.fbx", "Assets/Shaders/Albedo");

	////Entity Player = GameWorld->CreateEntity();
	////Player.AddComponent<Transform>();
	////Player.AddComponent<Model>("Assets/Models/nanosuit.obj", "Assets/Shaders/Albedo");

	////Entity Cube = GameWorld->CreateEntity();
	////Cube.AddComponent<Transform>("Fucked up cube");
	////Cube.AddComponent<Model>("Assets/Models/cube.obj", "Assets/Shaders/Albedo");

	const int Lights = 31;
	srand(13);
	for (unsigned int i = 0; i < Lights; i++)
	{
		Entity TestLight = GameWorld->CreateEntity();
		Transform& LightTransform = TestLight.AddComponent<Transform>("Light " + std::to_string(i));
		Light& LightInfo = TestLight.AddComponent<Light>();

		float xPos = ((rand() % 100) / 100.0) * 6.0 - 3.0;
		float yPos = ((rand() % 100) / 100.0) * 6.0 - 4.0;
		float zPos = ((rand() % 100) / 100.0) * 6.0 - 3.0;
		LightTransform.SetPosition(glm::vec3(xPos, yPos, zPos));
	}

	//glm::vec3 cubePositions[] = {
	//	glm::vec3(0.0f,  0.0f,  0.0f),
	//	glm::vec3(2.0f,  5.0f, -15.0f),
	//	glm::vec3(-1.5f, -2.2f, -2.5f),
	//	glm::vec3(-3.8f, -2.0f, -12.3f),
	//	glm::vec3(2.4f, -0.4f, -3.5f),
	//	glm::vec3(-1.7f,  3.0f, -7.5f),
	//	glm::vec3(1.3f, -2.0f, -2.5f),
	//	glm::vec3(1.5f,  2.0f, -2.5f),
	//	glm::vec3(1.5f,  0.2f, -1.5f),
	//	glm::vec3(-1.3f,  1.0f, -1.5f)
	//};
	//Transform* previousTransform = nullptr;
	//for (int i = 0; i < 1; ++i)
	//{
	//	//Entity Cube = GameWorld->CreateEntity();
	//	//Transform& BGPos = Cube.AddComponent<Transform>("Grass Cubes???? " + std::to_string(i));
	//	//BGPos.SetPosition(glm::vec3(i * 2.f, 0.f, 0.f));
	//	//BGPos.SetScale(glm::vec3(.1f, .1f, .1f));
	//	//if (previousTransform)
	//	//{
	//	//	//BGPos.SetParent(*previousTransform);
	//	//}
	//	//Cube.AddComponent<Model>("Assets/Models/TerraBlend.fbx", "Assets/Shaders/Albedo");
	//	////Cube.AddComponent<DebugCube>();
	//	//Cubes.push_back(Cube);
	//	//previousTransform = &BGPos;
	//	//Cube.SetActive(true);
	//}

	FlyingCameraController = new FlyingCameraCore();
	GameWorld->AddCore<FlyingCameraCore>(*FlyingCameraController);
}

float totalTime = 0.f;
void MitchGame::Update(float DeltaTime)
{
	FlyingCameraController->Update(DeltaTime);
#if ME_PLATFORM_WIN64
	Input& Instance = Input::GetInstance();
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
#endif
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
			CurrentCamera->Zoom -= PrevMouseScroll.y - Input::GetInstance().GetMouseScrollOffset().y;
		if (CurrentCamera->Zoom <= 1.0f)
			CurrentCamera->Zoom = 1.0f;
		if (CurrentCamera->Zoom >= 45.0f)
			CurrentCamera->Zoom = 45.0f;
	}
	PrevMouseScroll = Input::GetInstance().GetMouseScrollOffset();
	Transform& TransformComponent = MainCamera.GetComponent<Transform>();
}

void MitchGame::End()
{
}